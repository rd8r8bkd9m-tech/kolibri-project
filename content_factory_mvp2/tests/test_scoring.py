from app.services.scoring import score_video, score_article


def test_video_scoring():
    result = score_video(0.8, 0.6, 0.7, 0.9, 0.5, 0.9)
    assert result.score > 0
    assert "view_velocity" in result.rationale


def test_article_scoring():
    result = score_article(0.7, 0.6, 0.5, 0.9, 0.8)
    assert result.score > 0
    assert "intent_match" in result.rationale
