'use strict';

const express       = require('express');
const bcrypt        = require('bcrypt');
const jwt           = require('jsonwebtoken');
const cors          = require('cors');
const helmet        = require('helmet');
const rateLimit     = require('express-rate-limit');
const { v4: uuid }  = require('uuid');
const path          = require('path');
const fs            = require('fs');
const { Low }       = require('lowdb');
const { JSONFile }  = require('lowdb/node');

// ── Config ────────────────────────────────────────────────────────────────────
const PORT       = process.env.PORT       || 8080;
const JWT_SECRET = process.env.JWT_SECRET || 'ImVerySecure!';
const BCRYPT_ROUNDS = 12;
const TOKEN_TTL     = '7d';          // JWT expiry
const SESSION_TTL_MS = 7 * 86400000; // 7 days in ms

// ── Database ──────────────────────────────────────────────────────────────────
const DATA_DIR = path.join(__dirname, '..', 'data');
fs.mkdirSync(DATA_DIR, { recursive: true });

const adapter = new JSONFile(path.join(DATA_DIR, 'db.json'));
const db = new Low(adapter, {
    users:    {},   // uid -> { uid, username, passwordHash, createdAt }
    sessions: {},   // token -> { uid, username, issuedAt, expiresAt }
    usernames: {},  // username_lower -> uid  (index for fast lookup)
});

async function initDB() {
    await db.read();
    db.data ||= { users: {}, sessions: {}, usernames: {} };
    await db.write();
}

// ── Helpers ───────────────────────────────────────────────────────────────────
function isValidUsername(name) {
    return typeof name === 'string' &&
        name.length >= 3 &&
        name.length <= 24 &&
        /^[A-Za-z0-9_\-]+$/.test(name);
}

function isValidPassword(pw) {
    return typeof pw === 'string' && pw.length >= 6 && pw.length <= 128;
}

function purgeExpiredSessions() {
    const now = Date.now();
    for (const [tok, sess] of Object.entries(db.data.sessions)) {
        if (sess.expiresAt < now) delete db.data.sessions[tok];
    }
}

// Verify a game session token (called by game server via HTTP)
function verifySessionToken(token) {
    const sess = db.data.sessions[token];
    if (!sess) return null;
    if (sess.expiresAt < Date.now()) {
        delete db.data.sessions[token];
        return null;
    }
    return sess;
}

// ── Express app ───────────────────────────────────────────────────────────────
const app = express();

app.use(helmet({
    contentSecurityPolicy: false, // relax for the web UI
}));
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, '..', 'public')));

// Rate limiters
const authLimiter = rateLimit({
    windowMs: 15 * 60 * 1000,
    max: 20,
    message: { error: 'Too many requests, try again later.' },
});

const verifyLimiter = rateLimit({
    windowMs: 1 * 60 * 1000,
    max: 120,
    message: { error: 'Too many verify requests.' },
});

// ── Routes ────────────────────────────────────────────────────────────────────

// POST /api/register
app.post('/api/register', authLimiter, async (req, res) => {
    const { username, password } = req.body || {};

    if (!isValidUsername(username))
        return res.status(400).json({ error: 'Username must be 3-24 chars (A-Z, 0-9, _, -).' });

    if (!isValidPassword(password))
        return res.status(400).json({ error: 'Password must be 6-128 characters.' });

    const key = username.toLowerCase();
    if (db.data.usernames[key])
        return res.status(409).json({ error: 'Username already taken.' });

    const uid          = uuid();
    const passwordHash = await bcrypt.hash(password, BCRYPT_ROUNDS);

    db.data.users[uid]     = { uid, username, passwordHash, createdAt: Date.now() };
    db.data.usernames[key] = uid;
    await db.write();

    // Issue session token
    const token     = jwt.sign({ uid, username }, JWT_SECRET, { expiresIn: TOKEN_TTL });
    const expiresAt = Date.now() + SESSION_TTL_MS;
    db.data.sessions[token] = { uid, username, issuedAt: Date.now(), expiresAt };
    await db.write();

    return res.status(201).json({ token, username, uid });
});

// POST /api/login
app.post('/api/login', authLimiter, async (req, res) => {
    const { username, password } = req.body || {};

    if (!username || !password)
        return res.status(400).json({ error: 'Username and password required.' });

    const key = username.toLowerCase();
    const uid = db.data.usernames[key];
    if (!uid) return res.status(401).json({ error: 'Invalid username or password.' });

    const user = db.data.users[uid];
    const ok   = await bcrypt.compare(password, user.passwordHash);
    if (!ok) return res.status(401).json({ error: 'Invalid username or password.' });

    // Purge old sessions for this user (keep it clean)
    for (const [tok, sess] of Object.entries(db.data.sessions))
        if (sess.uid === uid) delete db.data.sessions[tok];

    const token     = jwt.sign({ uid, username: user.username }, JWT_SECRET, { expiresIn: TOKEN_TTL });
    const expiresAt = Date.now() + SESSION_TTL_MS;
    db.data.sessions[token] = { uid, username: user.username, issuedAt: Date.now(), expiresAt };
    await db.write();

    return res.json({ token, username: user.username, uid });
});

// POST /api/logout
app.post('/api/logout', (req, res) => {
    const token = (req.headers.authorization || '').replace('Bearer ', '').trim();
    if (token && db.data.sessions[token]) {
        delete db.data.sessions[token];
        db.write().catch(() => {});
    }
    return res.json({ ok: true });
});

// GET /api/verify?token=<token>
// Called by the game server to validate a connecting player's token.
// Returns: { valid: true, uid, username } or { valid: false }
app.get('/api/verify', verifyLimiter, (req, res) => {
    const token = req.query.token || '';
    const sess  = verifySessionToken(token);
    if (!sess) return res.json({ valid: false });

    // Verify JWT signature too
    try {
        jwt.verify(token, JWT_SECRET);
    } catch {
        delete db.data.sessions[token];
        db.write().catch(() => {});
        return res.json({ valid: false });
    }

    return res.json({ valid: true, uid: sess.uid, username: sess.username });
});

// GET /api/me — check own token from client UI
app.get('/api/me', (req, res) => {
    const token = (req.headers.authorization || '').replace('Bearer ', '').trim();
    const sess  = verifySessionToken(token);
    if (!sess) return res.status(401).json({ error: 'Not authenticated.' });
    return res.json({ uid: sess.uid, username: sess.username });
});

// ── Start ─────────────────────────────────────────────────────────────────────
initDB().then(() => {
    // Purge expired sessions every hour
    setInterval(() => {
        purgeExpiredSessions();
        db.write().catch(() => {});
    }, 3600 * 1000);

    app.listen(PORT, () => {
        console.log(`[Auth] Listening on http://0.0.0.0:${PORT}`);
        console.log(`[Auth] Data dir: ${DATA_DIR}`);
    });
}).catch(err => {
    console.error('[Auth] Init failed:', err);
    process.exit(1);
});