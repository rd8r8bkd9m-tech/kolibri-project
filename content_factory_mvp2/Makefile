.PHONY: up demo test seed

up:
	docker compose -f docker-compose.yml up -d --build

seed:
	docker compose -f docker-compose.yml exec backend python -m app.wait_for_db
	docker compose -f docker-compose.yml exec backend python -c "from app.db import SessionLocal; from app.seed import seed; db=SessionLocal(); print(seed(db)); db.close()"

demo:
	docker compose -f docker-compose.yml up -d --build
	docker compose -f docker-compose.yml exec backend python -m app.wait_for_db
	docker compose -f docker-compose.yml exec backend python -c "from app.db import SessionLocal; from app.seed import seed; from app.demo import run_demo; db=SessionLocal(); seed(db); print(run_demo(db)); db.close()"

test:
	docker compose -f docker-compose.yml exec backend pytest -q
