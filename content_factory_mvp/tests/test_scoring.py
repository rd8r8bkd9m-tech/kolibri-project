from app.services.best_video_finder import find_best_videos


def test_scoring_order():
    videos = find_best_videos("AI", 5)
    assert len(videos) == 5
    scores = [v.score for v in videos]
    assert scores == sorted(scores, reverse=True)
