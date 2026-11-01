# frozen_string_literal: true

require_relative "test_helper"
require "tmpdir"

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
    { png: png(w, h, px), bmp: bmp(w, h, px), tga: tga(w, h, px), ppm: ppm(w, h, px), gif: gif(w, h, px) }
      .each do |fmt, blob|
        assert_equal %w[#143cc8 #ed1c24], extract(blob, colors: 2).hex.sort, "format #{fmt}"
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
