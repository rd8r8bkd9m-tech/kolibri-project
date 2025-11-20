-- Kolibri Knowledge Base schema
CREATE TABLE IF NOT EXISTS documents (
    doc_id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    class TEXT NOT NULL,
    entropy REAL NOT NULL,
    bytes INTEGER NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE VIRTUAL TABLE IF NOT EXISTS documents_fts USING fts5(
    content,
    content='documents',
    content_rowid='doc_id'
);

CREATE TABLE IF NOT EXISTS embeddings (
    embedding_id INTEGER PRIMARY KEY AUTOINCREMENT,
    doc_id INTEGER NOT NULL,
    vector BLOB NOT NULL,
    dims INTEGER NOT NULL,
    residual_ref TEXT,
    FOREIGN KEY (doc_id) REFERENCES documents(doc_id)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_documents_sha ON documents(sha256);
CREATE INDEX IF NOT EXISTS idx_embeddings_doc ON embeddings(doc_id);
