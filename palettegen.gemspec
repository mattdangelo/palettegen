# frozen_string_literal: true

require_relative "lib/palettegen/version"

Gem::Specification.new do |spec|
  spec.name = "palettegen"
  spec.version = Palettegen::VERSION
  spec.authors = ["Matt D'Angelo"]
  spec.summary = "Fast k-means colour palette extraction from images (native extension)"
  spec.description = <<~DESC
    Extracts a 2-10 colour palette from JPEG, PNG, GIF, BMP, TGA, PSD, HDR, PNM
    (and WebP when libwebp is available) using k-means clustering in CIELAB.
    Returns hex codes or renders the palette as a JPEG/PNG strip of squares.
    No ImageMagick/OpenCV required.
  DESC
  spec.license = "MIT"
  spec.homepage = "https://github.com/mattdangelo/palettegen"
  spec.metadata = {
    "source_code_uri" => spec.homepage,
    "bug_tracker_uri" => "#{spec.homepage}/issues",
    "rubygems_mfa_required" => "true"
  }
  spec.required_ruby_version = ">= 3.0"

  spec.files = Dir[
    "lib/**/*.rb",
    "ext/**/*.{rb,cpp,hpp,h}",
    "README.md",
    "LICENSE*"
  ]
  spec.require_paths = ["lib"]
  spec.extensions = ["ext/palettegen/extconf.rb"]

  spec.add_development_dependency "minitest", "~> 5.0"
  spec.add_development_dependency "rake", "~> 13.0"
  spec.add_development_dependency "rake-compiler", "~> 1.2"
end
