# frozen_string_literal: true

module Palettegen
  # An ordered list of colours, most dominant first.
  class Palette
    include Enumerable

    DEFAULT_SQUARE = 100
    DEFAULT_QUALITY = 95 # > 90 disables chroma subsampling, keeping swatch edges clean

    attr_reader :colors

    def initialize(colors)
      @colors = colors.freeze
    end

    def each(&block)
      colors.each(&block)
    end

    def size
      colors.size
    end

    # => ["#aabbcc", ...]
    def hex
      colors.map(&:hex)
    end

    # JPEG bytes: one `square` x `square` block per colour, left to right.
    def to_jpeg(square: DEFAULT_SQUARE, quality: DEFAULT_QUALITY)
      Native.encode(colors.map(&:rgb), Integer(square), 0, Integer(quality))
    end

    # PNG bytes. Lossless, so pixel values exactly match #hex (JPEG may be off by ±1).
    def to_png(square: DEFAULT_SQUARE)
      Native.encode(colors.map(&:rgb), Integer(square), 1, 100)
    end

    # Write the palette image; format is chosen from the extension (.jpg/.jpeg/.png).
    # Returns the path.
    def save(path, square: DEFAULT_SQUARE, quality: DEFAULT_QUALITY)
      data =
        case File.extname(path.to_s).downcase
        when ".jpg", ".jpeg" then to_jpeg(square: square, quality: quality)
        when ".png" then to_png(square: square)
        else raise ArgumentError, "unsupported output format #{File.extname(path.to_s).inspect} (use .jpg or .png)"
        end
      File.binwrite(path, data)
      path
    end

    def to_a
      colors.dup
    end

    def inspect
      "#<Palettegen::Palette #{hex.join(' ')}>"
    end
  end
end
