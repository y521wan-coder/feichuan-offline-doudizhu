-- Apply only to the dedicated online PostgreSQL database.
CREATE TABLE IF NOT EXISTS online_schema_version (
    version integer PRIMARY KEY,
    applied_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS online_accounts (
    username text PRIMARY KEY,
    public_id text NOT NULL UNIQUE,
    nickname text NOT NULL,
    password_hash text NOT NULL,
    accept_invites boolean NOT NULL DEFAULT true,
    created_at timestamptz NOT NULL DEFAULT now(),
    CHECK (username = lower(username))
);

CREATE TABLE IF NOT EXISTS online_sessions (
    token_hash text PRIMARY KEY,
    username text NOT NULL REFERENCES online_accounts(username),
    created_at timestamptz NOT NULL DEFAULT now(),
    revoked_at timestamptz
);

CREATE TABLE IF NOT EXISTS online_rooms (
    room_id text PRIMARY KEY,
    player_count integer NOT NULL CHECK (player_count IN (2,3,4)),
    seats jsonb NOT NULL,
    status text NOT NULL,
    state jsonb,
    round_id text,
    seq bigint NOT NULL,
    deadline_ms bigint NOT NULL,
    turn_seconds integer NOT NULL CHECK (turn_seconds IN (15,30,60,120)),
    auto_used boolean NOT NULL,
    host text,
    password_hash text
);

CREATE TABLE IF NOT EXISTS online_requests (
    account text NOT NULL,
    request_id uuid NOT NULL,
    response jsonb NOT NULL,
    PRIMARY KEY(account, request_id)
);

CREATE TABLE IF NOT EXISTS online_results (
    round_id text PRIMARY KEY,
    room_id text NOT NULL,
    player_count integer NOT NULL,
    eligible boolean NOT NULL,
    result jsonb NOT NULL,
    completed_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS online_stats (
    username text NOT NULL REFERENCES online_accounts(username),
    player_count integer NOT NULL,
    games integer NOT NULL DEFAULT 0,
    wins integer NOT NULL DEFAULT 0,
    PRIMARY KEY(username, player_count)
);

INSERT INTO online_schema_version(version) VALUES(1) ON CONFLICT DO NOTHING;
