from app.services.analytics import calc_romi


def test_romi():
    result = calc_romi(leads=10, revenue=200.0, margin=120.0, cost=40.0)
    assert result.romi == 200.0
    assert result.roas == 5.0
