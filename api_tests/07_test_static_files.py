"""Static files serving test"""



def test_static_files(api):
    """Static files test"""
    static_files = [
        ("", "text/html"),
        ("index.css", "text/css"),
        ("index.js", "application/javascript"),
        ("favicon.webp", "image/webp")
    ]

    for path, expected_content_type in static_files:
        response = api.get_static_file(path)
        assert response.status_code == 200

        content_type = response.headers.get("content-type", "")
        assert expected_content_type in content_type.lower(), f"Incorrect Content-Type for {path}: expected '{expected_content_type}', got '{content_type}'"

        assert len(response.content) > 0

        print(f"✓ Static file {path or 'index'} accessible")
