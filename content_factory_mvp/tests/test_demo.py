from app.db import SessionLocal
from app.seed import seed
from app.demo import run_demo


def test_demo_pipeline():
    db = SessionLocal()
    seed(db)
    result = run_demo(db)
    db.close()
    assert result["status"] == "done"
