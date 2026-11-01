# frozen_string_literal: true

require_relative "test_helper"
require "tmpdir"
require "timeout"

class TestPalettegen < Minitest::Test
  include ImageFixtures

  RED = [237, 28, 36].freeze
  WHITE = [255, 255, 255].freeze
  BLUE = [20, 60, 200].freeze
  GREEN = [30, 160, 70].freeze

  def extract(blob, **opts)
    Palettegen.from_blob(blob, **opts)
  end

  # --- Correctness ----------------------------------------------------------

  def test_exact_colours_and_shares_for_flat_image
    palette = extract(png(*stripes([RED, WHITE, BLUE, GREEN])), colors: 4)
    assert_equal [RED, WHITE, BLUE, GREEN].map { |c| format("#%02x%02x%02x", *c) }.sort, palette.hex.sort
    palette.each { |c| assert_in_delta 0.25, c.share, 1e-9 }
  end

  def test_never_returns_duplicate_colours
    # Old engine: centroids seeded from pixel *positions*, so flat images gave duplicates.
    palette = extract(png(*stripes([RED, WHITE])), colors: 10)
    assert_equal 2, palette.size
    assert_equal palette.hex.uniq, palette.hex
  end

  def test_sorted_by_dominance
    w, h, px = stripes([RED, RED, RED, BLUE])
    palette = extract(png(w, h, px), colors: 2)
    assert_equal "#ed1c24", palette.colors.first.hex
    assert_in_delta 0.75, palette.colors.first.share, 1e-9
  end

  def test_transparent_pixels_are_ignored
    # Old engine dropped alpha, so a transparent background (often stored as
    # black or arbitrary RGB) leaked into the palette.
    px = [[0, 0, 0, 0]] * 12 + [RED + [255]] * 4
    palette = extract(png(4, 4, px), colors: 3)
    assert_equal ["#ed1c24"], palette.hex
    assert_in_delta 1.0, palette.colors.first.share, 1e-9
  end

  def test_alpha_threshold_is_configurable
    px = [BLUE + [100]] * 8 + [RED + [255]] * 8
    assert_equal ["#ed1c24"], extract(png(4, 4, px), colors: 2).hex
    assert_equal 2, extract(png(4, 4, px), colors: 2, alpha_threshold: 50).size
  end

  def test_fully_transparent_image_raises
    error = assert_raises(Palettegen::Error) { extract(png(2, 2, [[9, 9, 9, 0]] * 4), colors: 2) }
    assert_match(/no opaque pixels/, error.message)
  end

  def test_clusters_noisy_image_into_its_groups
    rng = Random.new(42)
    groups = [RED, BLUE, GREEN]
    px = Array.new(60 * 60) do |i|
      base = groups[(i % 60) / 20]
      base.map { |v| (v + rng.rand(-12..12)).clamp(0, 255) }
    end
    palette = extract(png(60, 60, px), colors: 3)
    assert_equal 3, palette.size
    groups.each do |g|
      nearest = palette.colors.min_by { |c| c.rgb.zip(g).sum { |a, b| (a - b)**2 } }
      nearest.rgb.zip(g).each { |a, b| assert_in_delta b, a, 8, "expected #{g} near #{nearest.hex}" }
      assert_in_delta 1.0 / 3, nearest.share, 0.01
    end
  end

  def test_rgb_colour_space
    palette = extract(png(*stripes([RED, BLUE])), colors: 2, color_space: :rgb)
    assert_equal %w[#143cc8 #ed1c24], palette.hex.sort
  end

  def test_large_image_uses_sampling
    w = h = 700 # 490k pixels > default 250k samples
    px = Array.new(w * h) { |i| (i % w) < w / 2 ? RED : BLUE }
    palette = extract(png(w, h, px), colors: 2)
    assert_equal %w[#143cc8 #ed1c24], palette.hex.sort
    palette.each { |c| assert_in_delta 0.5, c.share, 0.01 }
  end

  def test_deterministic_by_default
    px = Array.new(40 * 40) { |i| [i % 256, (i * 7) % 256, (i * 13) % 256] }
    blob = png(40, 40, px)
    assert_equal extract(blob, colors: 6).hex, extract(blob, colors: 6).hex
    assert_equal extract(blob, colors: 6, seed: 99).hex, extract(blob, colors: 6, seed: 99).hex
  end

  def test_thread_safety
    px = Array.new(80 * 80) { |i| [i % 256, (i * 7) % 256, (i * 13) % 256] }
    blob = png(80, 80, px)
    expected = extract(blob, colors: 5).hex
    results = Array.new(6) { Thread.new { extract(blob, colors: 5).hex } }.map(&:value)
    results.each { |r| assert_equal expected, r }
  end

  # --- Input formats --------------------------------------------------------

  def test_input_formats
    w, h, px = stripes([RED, BLUE], width_per: 2, height: 2)
    { png: png(w, h, px), bmp: bmp(w, h, px), gif: gif(w, h, px) }.each do |fmt, blob|
      assert_equal %w[#143cc8 #ed1c24], extract(blob, colors: 2).hex.sort, "format #{fmt}"
    end
  end

  def test_formats_outside_the_allowlist_are_rejected
    # TGA/PNM/PSD/HDR decoders are compiled out to shrink the attack surface.
    w, h, px = stripes([RED, BLUE], width_per: 2, height: 2)
    { tga: tga(w, h, px), ppm: ppm(w, h, px) }.each do |fmt, blob|
      assert_raises(Palettegen::DecodeError, "format #{fmt}") { extract(blob, colors: 2) }
    end
  end

  def test_jpeg_input_round_trip
    jpeg = Palettegen::Palette.new([Palettegen::Color.new(*RED, 0.5), Palettegen::Color.new(*BLUE, 0.5)]).to_jpeg
    palette = extract(jpeg, colors: 2)
    palette.colors.sort_by(&:b).zip([RED, BLUE]).each do |got, want|
      got.rgb.zip(want).each { |a, b| assert_in_delta b, a, 3 }
    end
  end

  def test_sample_images
    %w[apple.png green.png stripes.png].each do |name|
      palette = Palettegen.from_file(fixture(name), colors: 5)
      assert_operator palette.size, :>=, 1
      assert_equal palette.hex.uniq, palette.hex
      assert_in_delta 1.0, palette.sum(&:share), 1e-9
    end
    assert_equal ["#00ff00"], Palettegen.hex(fixture("green.png"), colors: 3)
  end

  def test_webp_without_support_gives_clear_error
    skip "built with WebP" if Palettegen.webp_supported?
    error = assert_raises(Palettegen::DecodeError) { extract("RIFF\x00\x00\x00\x00WEBPVP8 ".b, colors: 2) }
    assert_match(/WebP/, error.message)
  end

  # --- Size limits ----------------------------------------------------------

  def test_huge_declared_dimensions_are_rejected_before_decoding
    # Headers only: nothing is decoded, so this must be instant.
    [[500_000, 500_000], [65_536, 65_537], [40_000, 40_000]].each do |w, h|
      error = assert_raises(Palettegen::ImageTooLargeError) { extract(png_header(w, h)) }
      assert_match(/too large/, error.message)
    end
    assert_raises(Palettegen::ImageTooLargeError) { extract(bmp_header(100_000, -100_000)) }
  end

  def test_default_pixel_limit_is_50_megapixels
    assert_equal 50_000_000, Palettegen::DEFAULT_MAX_PIXELS
    assert_raises(Palettegen::ImageTooLargeError) { extract(png_header(10_000, 5_001)) }
    # 10000x5000 is exactly at the limit: it gets past the size check and fails on the (missing) pixel data.
    error = assert_raises(Palettegen::DecodeError) { extract(png_header(10_000, 5_000)) }
    refute_kind_of Palettegen::ImageTooLargeError, error
  end

  def test_max_pixels_option
    blob = png(*stripes([RED, BLUE], width_per: 5, height: 10)) # 10x10
    assert_raises(Palettegen::ImageTooLargeError) { extract(blob, max_pixels: 99) }
    assert_equal 2, extract(blob, max_pixels: 100).size
    assert_raises(Palettegen::ImageTooLargeError) { extract(bmp(10, 10, [RED] * 100), max_pixels: 99) }
    assert_raises(Palettegen::ImageTooLargeError) { extract(gif(10, 10, [RED] * 100), max_pixels: 99) }
    jpeg = Palettegen::Palette.new([Palettegen::Color.new(*RED, 1.0)]).to_jpeg(square: 10)
    assert_raises(Palettegen::ImageTooLargeError) { extract(jpeg, max_pixels: 99) }
  end

  def test_max_bytes_option
    blob = png(*stripes([RED, BLUE]))
    error = assert_raises(Palettegen::ImageTooLargeError) { extract(blob, max_bytes: blob.bytesize - 1) }
    assert_match(/#{blob.bytesize} bytes/, error.message)
    assert extract(blob, max_bytes: blob.bytesize)
    assert_equal 50 * 1024 * 1024, Palettegen::DEFAULT_MAX_BYTES
  end

  def test_from_file_checks_size_before_reading
    size = File.size(fixture("apple.png"))
    File.stub(:binread, ->(*) { flunk "file was read" }) do
      assert_raises(Palettegen::ImageTooLargeError) do
        Palettegen.from_file(fixture("apple.png"), max_bytes: size - 1)
      end
    end
  end

  def test_too_large_error_is_a_decode_error
    assert_operator Palettegen::ImageTooLargeError, :<, Palettegen::DecodeError
    assert_operator Palettegen::DecodeError, :<, Palettegen::Error
  end

  def test_limit_option_validation
    blob = png(*stripes([RED, BLUE]))
    [0, -1, "5", nil, 1.5].each do |bad|
      assert_raises(ArgumentError, "max_pixels: #{bad.inspect}") { extract(blob, max_pixels: bad) }
      assert_raises(ArgumentError, "max_bytes: #{bad.inspect}") { extract(blob, max_bytes: bad) }
    end
  end

  # --- Robustness -----------------------------------------------------------

  # Random corruption of valid images must only ever raise Palettegen::Error
  # (never crash). Most useful in CI's AddressSanitizer job.
  def test_corrupted_inputs_fail_cleanly
    rng = Random.new(1234)
    w, h, px = stripes([RED, WHITE, BLUE, GREEN], width_per: 3, height: 3)
    jpeg = Palettegen::Palette.new([RED, BLUE, GREEN].map { |c| Palettegen::Color.new(*c, 0.3) }).to_jpeg(square: 8)
    seeds = [png(w, h, px), bmp(w, h, px), gif(4, 2, [RED, BLUE] * 4), jpeg, File.binread(fixture("apple.png"))]
    iterations = Integer(ENV.fetch("PALETTEGEN_FUZZ_ITERATIONS", 300))
    iterations.times do |i|
      blob = seeds[i % seeds.size].dup
      case rng.rand(3)
      when 0 then rng.rand(1..8).times { blob.setbyte(rng.rand(blob.bytesize), rng.rand(256)) }
      when 1 then blob = blob[0, rng.rand(blob.bytesize)]
      else blob.insert(rng.rand(blob.bytesize), rng.bytes(rng.rand(1..16)))
      end
      begin
        extract(blob, colors: 3)
      rescue Palettegen::Error
        # expected for most mutations
      end
    end
  end

  def test_long_extraction_can_be_interrupted
    rng = Random.new(7)
    blob = png(400, 400, Array.new(400 * 400) { [rng.rand(256), rng.rand(256), rng.rand(256)] })
    started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    extract(blob, colors: 10)
    full = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started
    skip "extraction too fast to measure interruption (#{full.round(3)}s)" if full < 0.3

    started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    assert_raises(Timeout::Error) { Timeout.timeout(full / 10) { extract(blob, colors: 10) } }
    elapsed = Process.clock_gettime(Process::CLOCK_MONOTONIC) - started
    assert_operator elapsed, :<, full * 0.6, "took #{elapsed.round(3)}s to interrupt a #{full.round(3)}s extraction"
  end

  # --- Output ---------------------------------------------------------------

  def test_hex_formatting
    assert_equal "#ff0010", Palettegen::Color.new(255, 0, 16, 1.0).hex
  end

  def test_jpeg_output_is_strip_of_equal_squares
    palette = extract(png(*stripes([RED, WHITE, BLUE])), colors: 3)
    jpeg = palette.to_jpeg(square: 50)
    assert_equal Encoding::BINARY, jpeg.encoding
    assert jpeg.start_with?("\xFF\xD8".b), "not a JPEG"
    assert_equal [150, 50], jpeg_dimensions(jpeg)

    # Decode it back and check each square's colour.
    back = extract(jpeg, colors: 3)
    assert_equal 3, back.size
    back.each { |c| assert_in_delta 1.0 / 3, c.share, 0.02 }
  end

  def test_png_output_is_exact
    palette = extract(png(*stripes([RED, BLUE])), colors: 2)
    back = extract(palette.to_png(square: 10), colors: 2)
    assert_equal palette.hex.sort, back.hex.sort
  end

  def test_save_and_one_shot_jpeg
    Dir.mktmpdir do |dir|
      palette = Palettegen.from_file(fixture("stripes.png"), colors: 2)
      jpg = palette.save(File.join(dir, "p.jpg"))
      png_path = palette.save(File.join(dir, "p.PNG"))
      assert File.binread(jpg).start_with?("\xFF\xD8".b)
      assert File.binread(png_path).start_with?("\x89PNG".b)
      assert_raises(ArgumentError) { palette.save(File.join(dir, "p.gif")) }

      out = Palettegen.jpeg(fixture("stripes.png"), colors: 2, to: File.join(dir, "one.jpg"))
      assert File.exist?(out)
      assert Palettegen.jpeg(fixture("stripes.png"), colors: 2).start_with?("\xFF\xD8".b)
    end
  end

  # --- Validation -----------------------------------------------------------

  def test_colors_must_be_between_2_and_10
    blob = png(*stripes([RED, BLUE]))
    [1, 11, 0, -3, "5", 2.5, nil].each do |bad|
      assert_raises(ArgumentError, "colors: #{bad.inspect}") { extract(blob, colors: bad) }
    end
    assert extract(blob, colors: 2)
    assert extract(blob, colors: 10)
  end

  def test_option_validation
    blob = png(*stripes([RED, BLUE]))
    assert_raises(ArgumentError) { extract(blob, color_space: :hsv) }
    assert_raises(ArgumentError) { extract(blob, seed: -1) }
    assert_raises(ArgumentError) { extract(blob, max_samples: 0) }
    assert_raises(ArgumentError) { extract(blob, alpha_threshold: 256) }
    assert_raises(ArgumentError) { extract(nil) }
  end

  def test_corrupt_and_empty_input
    assert_raises(Palettegen::DecodeError) { extract("") }
    assert_raises(Palettegen::DecodeError) { extract("definitely not an image") }
    assert_raises(Palettegen::DecodeError) { extract(png(*stripes([RED]))[0, 40]) } # truncated
    assert_raises(Errno::ENOENT) { Palettegen.from_file("/no/such/file.png") }
  end

  private

  # A PNG that declares w x h but has no pixel data.
  def png_header(width, height)
    ihdr = [width, height, 8, 6, 0, 0, 0].pack("NNCCCCC")
    [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A].pack("C*") + [13].pack("N") + "IHDR" + ihdr + [Zlib.crc32("IHDR#{ihdr}")].pack("N")
  end

  # A BMP header declaring w x h (h negative = top-down) with no pixel data.
  def bmp_header(width, height)
    ["BM", 54, 0, 0, 54].pack("a2VvvV") + [40, width, height, 1, 24, 0, 0, 2835, 2835, 0, 0].pack("VVVvvVVVVVV")
  end

  def jpeg_dimensions(data)
    i = 2
    while i < data.bytesize
      marker = data.getbyte(i + 1)
      len = data[i + 2, 2].unpack1("n")
      return data[i + 5, 4].unpack("nn").reverse if (0xC0..0xC2).cover?(marker)

      i += 2 + len
    end
  end
end
