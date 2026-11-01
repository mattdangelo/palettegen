# frozen_string_literal: true

module Palettegen
  # One palette entry. `share` is the fraction (0.0..1.0) of the image's opaque
  # pixels that belong to this colour.
  Color = Struct.new(:r, :g, :b, :share) do
    def hex
      format("#%02x%02x%02x", r, g, b)
    end
    alias_method :to_s, :hex

    def rgb
      [r, g, b]
    end

    def inspect
      format("#<Palettegen::Color %s %.1f%%>", hex, share * 100)
    end
  end
end
