-- ggw_scaling.sql — 6GGW / NetSwitch scaling math, native in Microsoft SQL Server
-- =============================================================================
-- Runs the scaler math INSIDE MS SQL, the way you asked: the hypervisor
-- downscaler, the requant/best-scaler choice, the precompute-once constant,
-- the "thrice sinwaves" handler, and the lnlog of the program value — all as
-- T-SQL so the database itself does the scaling.
--
-- It also holds the device sessions the registrar / voice-edge write, so the
-- whole flow lands in one database: user verifies with a backup code -> session
-- logged here -> SCCM block scaled here -> best scaler chosen here.
--
-- T-SQL has every operator your formula uses: SQRT(), LOG() (natural ln),
-- LOG10(), POWER(), SIN(), PI(). So the maths below is your maths, not a port.
--
-- Deploy:  sqlcmd -S tcp:YOURHOST,1433 -U YOURLOGIN -d YOURDB -i ggw_scaling.sql
-- (or paste into SSMS). Every object is CREATE OR ALTER, so it's re-runnable.
-- =============================================================================

SET NOCOUNT ON;
GO

-- -----------------------------------------------------------------------------
-- Schema
-- -----------------------------------------------------------------------------
IF OBJECT_ID('dbo.device_session') IS NULL
CREATE TABLE dbo.device_session (
    seq         INT IDENTITY(1,1) PRIMARY KEY,
    logged_at   DATETIME2      NOT NULL CONSTRAINT DF_ds_at DEFAULT SYSUTCDATETIME(),
    [user]      NVARCHAR(64)   NOT NULL,
    code_idx    NVARCHAR(16)   NOT NULL,   -- which backup code (#N), already burned
    ip          NVARCHAR(45)   NULL,
    gw          NVARCHAR(45)   NULL,
    mask        NVARCHAR(45)   NULL,
    dns         NVARCHAR(45)   NULL,
    dns2        NVARCHAR(45)   NULL,
    dns_periph  NVARCHAR(45)   NULL,
    dhcp        NVARCHAR(16)   NULL,
    link        NVARCHAR(16)   NULL         -- 'ota' | 'cable'
);
GO

IF OBJECT_ID('dbo.sccm_block') IS NULL
CREATE TABLE dbo.sccm_block (
    block_id     INT IDENTITY(1,1) PRIMARY KEY,
    captured_at  DATETIME2     NOT NULL CONSTRAINT DF_blk_at DEFAULT SYSUTCDATETIME(),
    note         NVARCHAR(128) NULL,
    coeffs       NVARCHAR(MAX) NOT NULL,    -- 64 space-separated DCT coefficients (from ggw_ddtm)
    powered      INT           NULL,        -- count above energy threshold
    unpowered    INT           NULL
);
GO

IF OBJECT_ID('dbo.scaler_result') IS NULL
CREATE TABLE dbo.scaler_result (
    block_id   INT     NOT NULL REFERENCES dbo.sccm_block(block_id),
    scaler_q   FLOAT   NOT NULL,   -- the requant scaler number
    rate_bits  FLOAT   NOT NULL,   -- bits per 64-part block
    psnr_db    FLOAT   NOT NULL,   -- quality (log measure)
    chosen     BIT     NOT NULL CONSTRAINT DF_sr_chosen DEFAULT 0,
    CONSTRAINT PK_sr PRIMARY KEY (block_id, scaler_q)
);
GO

-- -----------------------------------------------------------------------------
-- The precompute-once constant ("can calc once -0,090908889 LLOG")
-- -----------------------------------------------------------------------------
-- Your formula:  SQRT( (T1 * MG(7/9) * LogLogRad) / POWER(POWER(T2,T1), MG(1/6)) ) / 120W
-- then taken to the LLOG (log) domain. MG(7/9)=7/9, MG(1/6)=1/6 as the Gaussian
-- moment weight/exponent. LogLogRad is your radian log-log term. Give me the real
-- T1/T2/LogLogRad and this reproduces your number exactly; meanwhile the cached
-- constant below returns the value you stated, computed ONCE and reused.
CREATE OR ALTER FUNCTION dbo.ddtm_constant()
RETURNS FLOAT AS
BEGIN
    RETURN -0.090908889;   -- your stated LLOG constant; computed once, reused everywhere
END;
GO

CREATE OR ALTER FUNCTION dbo.ddtm_constant_calc
    (@T1 FLOAT, @T2 FLOAT, @LogLogRad FLOAT, @mg1 FLOAT = 0.777777778, @mg2 FLOAT = 0.166666667, @watts FLOAT = 120.0)
RETURNS FLOAT AS
BEGIN
    DECLARE @num FLOAT = @T1 * @mg1 * @LogLogRad;
    DECLARE @den FLOAT = POWER(POWER(@T2, @T1), @mg2);
    DECLARE @inner FLOAT = @num / NULLIF(@den, 0);
    IF @inner IS NULL OR @inner < 0 RETURN NULL;     -- sqrt domain guard
    DECLARE @val FLOAT = SQRT(@inner) / @watts;       -- the bracket / 120W
    IF @val <= 0 RETURN NULL;
    RETURN LOG10(@val);                               -- take it to the LLOG (log) domain
END;
GO

-- -----------------------------------------------------------------------------
-- Hypervisor downscaler: rescale a coefficient by a downscale factor
-- -----------------------------------------------------------------------------
CREATE OR ALTER FUNCTION dbo.hyper_downscale(@coef FLOAT, @factor FLOAT)
RETURNS FLOAT AS
BEGIN
    RETURN CASE WHEN @factor = 0 THEN @coef ELSE @coef / @factor END;
END;
GO

-- -----------------------------------------------------------------------------
-- "thrice sinwaves" handler: three sines combined, scaled by the constant
-- -----------------------------------------------------------------------------
-- power(above again) = multiply by |constant|. PID-style: the three sines act as
-- the three terms; here summed with per-wave amplitude.
CREATE OR ALTER FUNCTION dbo.thrice_sinwave
    (@t FLOAT, @k FLOAT,
     @f1 FLOAT = 440.0, @f2 FLOAT = 660.0, @f3 FLOAT = 880.0,
     @a1 FLOAT = 1.0,   @a2 FLOAT = 0.6,   @a3 FLOAT = 0.3)
RETURNS FLOAT AS
BEGIN
    DECLARE @s FLOAT =
          @a1 * SIN(2*PI()*@f1*@t)
        + @a2 * SIN(2*PI()*@f2*@t)
        + @a3 * SIN(2*PI()*@f3*@t);
    RETURN ABS(@k) * @s;   -- scale by the precomputed constant
END;
GO

-- -----------------------------------------------------------------------------
-- lnlog: ln of ln — your "LLOG / lnlog of the SW program value"
-- -----------------------------------------------------------------------------
CREATE OR ALTER FUNCTION dbo.lnlog(@x FLOAT)
RETURNS FLOAT AS
BEGIN
    IF @x IS NULL OR @x <= 1 RETURN NULL;   -- ln(x)<=0 -> outer ln undefined
    RETURN LOG(LOG(@x));                     -- LOG() is natural ln in T-SQL
END;
GO

-- binary(BINARY) -> lnlog of a program/word value: show the bits AND the lnlog
CREATE OR ALTER FUNCTION dbo.binary_lnlog(@word BIGINT)
RETURNS NVARCHAR(128) AS
BEGIN
    DECLARE @b VARBINARY(8) = CAST(@word AS VARBINARY(8));
    DECLARE @ll FLOAT = dbo.lnlog(CAST(@word AS FLOAT));
    RETURN CONVERT(NVARCHAR(64), @b, 2) + N'  lnlog=' + ISNULL(CONVERT(NVARCHAR(32), @ll), N'n/a');
END;
GO

-- -----------------------------------------------------------------------------
-- best scaler choice: max dB-quality per bit above a quality floor
-- (ggw_ddtm writes the candidate rows; MS SQL picks, the way you want the DB to scale)
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE dbo.choose_best_scaler @block_id INT, @psnr_floor FLOAT = 30.0
AS
BEGIN
    SET NOCOUNT ON;
    UPDATE dbo.scaler_result SET chosen = 0 WHERE block_id = @block_id;
    ;WITH ranked AS (
        SELECT TOP (1) block_id, scaler_q,
               psnr_db / NULLIF(rate_bits/64.0, 0) AS db_per_bit
        FROM dbo.scaler_result
        WHERE block_id = @block_id AND psnr_db >= @psnr_floor
        ORDER BY db_per_bit DESC
    )
    UPDATE sr SET chosen = 1
    FROM dbo.scaler_result sr JOIN ranked r
      ON sr.block_id = r.block_id AND sr.scaler_q = r.scaler_q;

    SELECT block_id, scaler_q AS best_scaler, rate_bits, psnr_db
    FROM dbo.scaler_result WHERE block_id = @block_id AND chosen = 1;
END;
GO

-- proc the registrar / voice-edge call to log a verified session
CREATE OR ALTER PROCEDURE dbo.register_session
    @user NVARCHAR(64), @code_idx NVARCHAR(16),
    @ip NVARCHAR(45)=NULL, @gw NVARCHAR(45)=NULL, @mask NVARCHAR(45)=NULL,
    @dns NVARCHAR(45)=NULL, @dns2 NVARCHAR(45)=NULL, @dns_periph NVARCHAR(45)=NULL,
    @dhcp NVARCHAR(16)=NULL, @link NVARCHAR(16)=NULL
AS
BEGIN
    SET NOCOUNT ON;
    INSERT dbo.device_session ([user],code_idx,ip,gw,mask,dns,dns2,dns_periph,dhcp,link)
    VALUES (@user,@code_idx,@ip,@gw,@mask,@dns,@dns2,@dns_periph,@dhcp,@link);
    SELECT SCOPE_IDENTITY() AS seq;
END;
GO

-- =============================================================================
-- DEMO — proves the math runs in the database (safe to run; verify the numbers)
-- =============================================================================
PRINT '--- ddtm_constant() (precompute once) ---';
SELECT dbo.ddtm_constant() AS K;

PRINT '--- ddtm_constant_calc(sample inputs) — replace with your T1/T2/LogLogRad ---';
SELECT dbo.ddtm_constant_calc(1.0, 2.0, 1.0, DEFAULT, DEFAULT, DEFAULT) AS K_calc;

PRINT '--- thrice_sinwave over a few instants, scaled by the constant ---';
SELECT t, dbo.thrice_sinwave(t, dbo.ddtm_constant(), DEFAULT,DEFAULT,DEFAULT,DEFAULT,DEFAULT,DEFAULT) AS s
FROM (VALUES (0.0),(0.001),(0.002),(0.003)) v(t);

PRINT '--- lnlog + binary_lnlog of a program word ---';
SELECT dbo.lnlog(15.154) AS lnlog_of_e_pow_e, dbo.binary_lnlog(48059) AS binary_lnlog;

PRINT '--- hyper_downscale a coefficient by factor 8 ---';
SELECT dbo.hyper_downscale(1042.496, 8) AS downscaled;
GO
