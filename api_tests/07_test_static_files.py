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


# Shell assets (index.html/js/css) are served gzipped with ETag-based revalidation.
SHELL_ASSETS = ("", "index.js", "index.css")

# Already-compressed binary assets must be served raw, never re-gzipped.
COMPRESSED_ASSETS = (
    "favicon.webp",
    "roboto-latin-wght-normal.woff2",
    "roboto-cyrillic-wght-normal.woff2",
    "roboto-cyrillic-ext-wght-normal.woff2",
)


def test_shell_assets_cache_headers(api):
    """Shell assets carry an ETag + Cache-Control: no-cache and stay gzipped (P0)."""
    for path in SHELL_ASSETS:
        response = api.session.get(f"{api.base_url}/{path}", timeout=10)
        assert response.status_code == 200, f"GET /{path} -> {response.status_code}"

        etag = response.headers.get("etag")
        assert etag, f"Missing/empty ETag for /{path or 'index'}: {etag!r}"

        cache_control = response.headers.get("cache-control")
        assert cache_control == "no-cache", \
            f"Cache-Control for /{path or 'index'} expected 'no-cache', got {cache_control!r}"

        # The ETag work must NOT have dropped gzip: shell assets stay gzipped.
        content_encoding = response.headers.get("content-encoding")
        assert content_encoding == "gzip", \
            f"Content-Encoding for /{path or 'index'} expected 'gzip', got {content_encoding!r}"

        print(f"✓ Shell asset /{path or 'index'} has ETag + no-cache + gzip")


def test_shell_assets_not_modified_304(api):
    """A matching If-None-Match yields an empty 304; a stale one yields a 200 body (P0)."""
    for path in SHELL_ASSETS:
        # First GET to read the asset's current ETag.
        first = api.session.get(f"{api.base_url}/{path}", timeout=10)
        assert first.status_code == 200, f"GET /{path} -> {first.status_code}"
        etag = first.headers.get("etag")
        assert etag, f"Missing/empty ETag for /{path or 'index'}: {etag!r}"

        # Second GET with the matching validator must short-circuit to 304 + empty body.
        revalidate = api.session.get(
            f"{api.base_url}/{path}", headers={"If-None-Match": etag}, timeout=10
        )
        assert revalidate.status_code == 304, \
            f"If-None-Match match for /{path or 'index'} expected 304, got {revalidate.status_code}"
        assert revalidate.content == b"", \
            f"304 for /{path or 'index'} must have an empty body, got {len(revalidate.content)} bytes"
        assert revalidate.headers.get("etag") == etag, \
            f"304 for /{path or 'index'} must echo the same ETag, got {revalidate.headers.get('etag')!r}"

        # A deliberately wrong validator must fall through to a normal 200 with a body.
        mismatch = api.session.get(
            f"{api.base_url}/{path}", headers={"If-None-Match": '"stale-nomatch"'}, timeout=10
        )
        assert mismatch.status_code == 200, \
            f"Stale If-None-Match for /{path or 'index'} expected 200, got {mismatch.status_code}"
        assert len(mismatch.content) > 0, \
            f"Stale If-None-Match for /{path or 'index'} must return a non-empty body"

        print(f"✓ Shell asset /{path or 'index'} revalidates: 304 on match, 200 on mismatch")


def test_compressed_assets_not_double_gzipped(api):
    """Already-compressed assets are served raw (no Content-Encoding: gzip) and intact (P2)."""
    for path in COMPRESSED_ASSETS:
        response = api.session.get(f"{api.base_url}/{path}", timeout=10)
        assert response.status_code == 200, f"GET /{path} -> {response.status_code}"

        # The server must NOT advertise gzip on assets that are already compressed.
        content_encoding = response.headers.get("content-encoding")
        assert content_encoding is None, \
            f"/{path} must not set Content-Encoding, got {content_encoding!r}"

        # Magic bytes confirm the asset is served un-gzipped and intact. A gzip stream
        # would start with \x1f\x8b, so this catches a double-gzip regression.
        if path.endswith(".woff2"):
            assert response.content[:4] == b"wOF2", \
                f"/{path} is not a raw woff2 (magic {response.content[:4]!r})"
            cache_control = response.headers.get("cache-control", "")
            assert "immutable" in cache_control, \
                f"/{path} Cache-Control should be immutable, got {cache_control!r}"
        elif path.endswith(".webp"):
            assert response.content[:4] == b"RIFF" and response.content[8:12] == b"WEBP", \
                f"/{path} is not a raw webp (magic {response.content[:12]!r})"

        print(f"✓ Compressed asset /{path} served raw and intact")
