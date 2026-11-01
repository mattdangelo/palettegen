# frozen_string_literal: true

require_relative "palettegen/version"

# Extract a colour palette from an image using k-means clustering.
#
#   Palettegen.hex("photo.jpg", colors: 5)   # => ["#2b3a4f", "#d9c8a1", ...]
#   Palettegen.jpeg("photo.jpg", colors: 5)  # => JPEG bytes (a strip of equal squares)
#
#   palette = Palettegen.from_file("photo.jpg", colors: 5)
#   palette.hex                  # => ["#2b3a4f", ...]
#   palette.save("palette.jpg")  # or .png
module Palettegen
  class Error < StandardError; end
  # Raised when image data can't be decoded (corrupt or unsupported format).
  class DecodeError < Error; end
  # Raised when the input exceeds max_bytes or the image exceeds max_pixels.
  # Rescue it to return e.g. HTTP 413 for oversized uploads.
  class ImageTooLargeError < DecodeError; end
end

require "palettegen/palettegen" # native extension
require_relative "palettegen/color"
require_relative "palettegen/palette"

module Palettegen
  MIN_COLORS = Native::MIN_COLORS
  MAX_COLORS = Native::MAX_COLORS
  DEFAULT_COLORS = 5
  DEFAULT_MAX_PIXELS = Native::DEFAULT_MAX_PIXELS # 50 megapixels
  DEFAULT_MAX_BYTES = 50 * 1024 * 1024            # 50 MB of encoded image data
  COLOR_SPACES = { lab: 0, rgb: 1 }.freeze

  class << self
    # Build a Palette from an image file on disk. The file's size is checked
    # against max_bytes before it is read.
    def from_file(path, max_bytes: DEFAULT_MAX_BYTES, **options)
      check_bytes(File.size(path), max_bytes)
      from_blob(File.binread(path), max_bytes: max_bytes, **options)
    end

    # Build a Palette from encoded image bytes (e.g. an upload or HTTP response).
    #
    # Options:
    #   colors:          number of colours, 2..10 (default 5). Fewer are returned
    #                    if the image doesn't contain that many distinct colours.
    #   color_space:     :lab (default, perceptual) or :rgb.
    #   seed:            Integer; same image + seed always gives the same palette.
    #   max_samples:     pixels sampled from large images (default 250_000).
    #   alpha_threshold: pixels with alpha below this (0..255) are ignored (default 128).
    #   max_bytes:       reject input larger than this many bytes (default 50 MB).
    #   max_pixels:      reject images whose width * height exceeds this (default
    #                    50 million). Checked from the header, before decoding.
    #
    # Raises ImageTooLargeError if either limit is exceeded.
    def from_blob(data, colors: DEFAULT_COLORS, color_space: :lab, seed: 0,
                  max_samples: 250_000, alpha_threshold: 128,
                  max_bytes: DEFAULT_MAX_BYTES, max_pixels: DEFAULT_MAX_PIXELS)
      raise ArgumentError, "image data must be a String" unless data.is_a?(String)

      check_bytes(data.bytesize, max_bytes)
      max_pixels = positive_integer(max_pixels, "max_pixels")

      colors = validate_colors(colors)
      space = COLOR_SPACES.fetch(color_space) do
        raise ArgumentError, "color_space must be one of #{COLOR_SPACES.keys.inspect}"
      end
      raise ArgumentError, "seed must be a non-negative Integer" unless seed.is_a?(Integer) && seed >= 0 && seed < 2**64

      swatches = Native.extract(data, colors, space, seed, Integer(max_samples), Integer(alpha_threshold), max_pixels)
      Palette.new(swatches.map { |r, g, b, share| Color.new(r, g, b, share) })
    end

    # One-shot: hex codes, e.g. ["#aabbcc", ...], most dominant first.
    def hex(path, **options)
      from_file(path, **options).hex
    end

    # One-shot: palette image as JPEG bytes. Pass `to:` to write it to a file.
    def jpeg(path, square: Palette::DEFAULT_SQUARE, quality: Palette::DEFAULT_QUALITY, to: nil, **options)
      palette = from_file(path, **options)
      return palette.save(to, square: square, quality: quality) if to

      palette.to_jpeg(square: square, quality: quality)
    end

    def webp_supported?
      Native.webp_supported?
    end

    private

    def check_bytes(size, max_bytes)
      max_bytes = positive_integer(max_bytes, "max_bytes")
      return if size <= max_bytes

      raise ImageTooLargeError, "image data is too large (#{size} bytes, limit is #{max_bytes})"
    end

    def positive_integer(value, name)
      raise ArgumentError, "#{name} must be a positive Integer, got #{value.inspect}" unless value.is_a?(Integer) && value.positive?

      value
    end

    def validate_colors(colors)
      unless colors.is_a?(Integer) && colors.between?(MIN_COLORS, MAX_COLORS)
        raise ArgumentError, "colors must be an Integer between #{MIN_COLORS} and #{MAX_COLORS}, got #{colors.inspect}"
      end

      colors
    end
  end
end
