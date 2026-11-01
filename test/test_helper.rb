# frozen_string_literal: true

$LOAD_PATH.unshift File.expand_path("../lib", __dir__)
require "palettegen"
require "minitest/autorun"
require "minitest/mock"
require "zlib"

# Minimal pure-Ruby encoders so tests can build exact fixtures in every format
# without depending on ImageMagick etc. `pixels` is an Array of [r, g, b(, a)]
# in row-major order.
module ImageFixtures
  FIXTURES = File.expand_path("fixtures", __dir__)

  module_function

  def fixture(name)
    File.join(FIXTURES, name)
  end

  # Horizontal stripes of equal width, one per colour.
  def stripes(colors, width_per: 4, height: 4)
    row = colors.flat_map { |c| [c] * width_per }
    [row.size, height, row * height]
  end

  def png(width, height, pixels)
    rgba = pixels.map { |p| p.size == 4 ? p : p + [255] }
    raw = rgba.each_slice(width).map { |row| "\x00".b + row.flatten.pack("C*") }.join
    chunk = lambda do |type, data|
      [data.bytesize].pack("N") + type + data + [Zlib.crc32(type + data)].pack("N")
    end
    "\x89PNG\r\n\x1a\n".b +
      chunk.call("IHDR", [width, height, 8, 6, 0, 0, 0].pack("NNCCCCC")) +
      chunk.call("IDAT", Zlib::Deflate.deflate(raw)) +
      chunk.call("IEND", "")
  end

  def bmp(width, height, pixels)
    row_size = (width * 3 + 3) & ~3
    rows = pixels.each_slice(width).map do |row|
      data = row.map { |r, g, b| [b, g, r].pack("C*") }.join
      data + "\x00" * (row_size - data.bytesize)
    end.reverse.join # bottom-up
    header = ["BM", 54 + rows.bytesize, 0, 0, 54].pack("a2VvvV")
    info = [40, width, height, 1, 24, 0, rows.bytesize, 2835, 2835, 0, 0].pack("VVVvvVVVVVV")
    header + info + rows
  end

  def tga(width, height, pixels)
    header = [0, 0, 2, 0, 0, 0, 0, 0, width, height, 24, 0x20].pack("CCCvvCvvvvCC") # top-left origin
    header + pixels.map { |r, g, b| [b, g, r].pack("C*") }.join
  end

  def ppm(width, height, pixels)
    "P6\n#{width} #{height}\n255\n".b + pixels.flatten.pack("C*")
  end

  # GIF with a <= 4 colour palette, using "uncompressed" LZW (a clear code every
  # two pixels keeps the code width fixed at 3 bits).
  def gif(width, height, pixels)
    palette = pixels.uniq
    raise ArgumentError, "max 4 colours" if palette.size > 4

    table = (palette + [[0, 0, 0]] * (4 - palette.size)).flatten.pack("C*")
    codes = []
    pixels.map { |p| palette.index(p) }.each_slice(2) { |pair| codes << 4 << pair[0]; codes << pair[1] if pair[1] }
    codes << 5
    bits = codes.map { |c| c.to_s(2).rjust(3, "0").reverse }.join
    bytes = bits.scan(/.{1,8}/).map { |b| b.ljust(8, "0").reverse.to_i(2) }.pack("C*")
    blocks = bytes.bytes.each_slice(255).map { |s| [s.size].pack("C") + s.pack("C*") }.join

    "GIF89a".b + [width, height, 0xF1, 0, 0].pack("vvCCC") + table +
      [0x2C, 0, 0, width, height, 0].pack("CvvvvC") + [2].pack("C") + blocks + "\x00;".b
  end
end
