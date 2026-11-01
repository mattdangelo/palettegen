# palettegen

Extract a colour palette (2–10 colours) from an image with k-means clustering,
implemented as a native C++ extension. No ImageMagick or OpenCV needed.

## Usage

```ruby
require "palettegen"

# Hex codes, most dominant colour first
Palettegen.hex("photo.jpg", colors: 5)
# => ["#2b3a4f", "#d9c8a1", "#8a5a3c", "#f4efe6", "#5e7d8c"]

# Palette image: a horizontal strip of equal squares, as JPEG bytes...
jpeg_bytes = Palettegen.jpeg("photo.jpg", colors: 5)
# ...or written straight to a file
Palettegen.jpeg("photo.jpg", colors: 5, to: "palette.jpg", square: 120)

# Or work with the palette object
palette = Palettegen.from_file("photo.jpg", colors: 6)
palette.hex                 # => ["#2b3a4f", ...]
palette.colors.first        # => #<Palettegen::Color #2b3a4f 41.3%>
palette.colors.first.share  # => 0.413 (fraction of the image's pixels)
palette.to_jpeg(square: 80) # => JPEG bytes
palette.to_png              # => PNG bytes (lossless)
palette.save("palette.png") # format picked from the extension

# From bytes in memory (uploads, HTTP responses, ActiveStorage...)
Palettegen.from_blob(params[:image].read, colors: 4).hex
```

### Options

| option            | default | |
|-------------------|---------|---|
| `colors`          | `5`     | 2..10. Fewer are returned if the image has fewer distinct colours (you never get duplicates). |
| `color_space`     | `:lab`  | `:lab` clusters perceptually (CIELAB); `:rgb` clusters in raw sRGB. |
| `seed`            | `0`     | Results are deterministic: the same image and seed always give the same palette. |
| `max_samples`     | `250_000` | Large images are sampled down to this many pixels. |
| `alpha_threshold` | `128`   | Pixels more transparent than this (0..255) are ignored. |
| `max_bytes`       | 50 MB   | Input larger than this is rejected (`from_file` checks the file size before reading it). |
| `max_pixels`      | `50_000_000` | Images whose width × height exceed this are rejected from the header, before any pixels are decoded. |

Palette image options: `square:` (pixels per swatch, default 100) and
`quality:` (JPEG, default 95).

### Errors

* `ArgumentError`: invalid options (e.g. `colors: 12`)
* `Palettegen::ImageTooLargeError`: input over `max_bytes` or `max_pixels` (a subclass of `DecodeError`)
* `Palettegen::DecodeError`: corrupt or unsupported image data
* `Palettegen::Error`: other failures (e.g. a fully transparent image)

All of these inherit from `Palettegen::Error` except `ArgumentError`.

### Untrusted input

The defaults are meant to be safe for user uploads:

* Oversized images are rejected from their header, so a tiny file that claims to
  be 500000 × 500000 never gets decoded. Peak memory per call is roughly
  `max_bytes + 4 × max_pixels` (about 250 MB with the defaults); lower the limits
  if many extractions run at once on a small server.
* Only the JPEG, PNG, GIF and BMP decoders (and WebP, if enabled) are compiled in.
* Clustering checks for interrupts, so `Timeout.timeout`, `Thread#raise` and
  Ctrl-C stop a long extraction promptly.

```ruby
begin
  Palettegen.from_blob(upload.read, max_pixels: 20_000_000).hex
rescue Palettegen::ImageTooLargeError
  head :payload_too_large
rescue Palettegen::Error
  head :unprocessable_entity
end
```

## Supported formats

JPEG, PNG (including 16-bit and transparency), GIF (first frame) and BMP. These
are decoded by the vendored [stb_image](https://github.com/nothings/stb), so there
are no system dependencies.

**WebP** is enabled automatically if libwebp's headers are present when the gem
is installed (`apt install libwebp-dev`, `brew install webp`,
`pacman -S mingw-w64-ucrt-x86_64-libwebp`). Check with `Palettegen.webp_supported?`.

TIFF, HEIC, AVIF, TGA, PSD and HDR aren't supported directly. Convert them first (e.g. with
`ruby-vips`) and pass the PNG bytes to `from_blob`.

Images over `max_pixels` (50 megapixels by default) are rejected to protect
servers from decompression bombs.

## How it works

1. Up to `max_samples` pixels are sampled (one random pixel per block), and
   transparent pixels are skipped.
2. Identical colours are merged into weighted points.
3. Colours are converted to CIELAB, so cluster averages match what the eye sees
   rather than muddy gamma-encoded RGB averages.
4. Weighted k-means++ seeding plus Lloyd iterations, best of 3 restarts. Empty
   clusters are re-seeded instead of left dead.
5. Centroids are converted back to sRGB and sorted by how much of the image they cover.

Clustering runs without Ruby's GVL, so it runs in parallel under Puma or Sidekiq
threads, and it can be interrupted (e.g. by `Timeout`).

## Development

### Prerequisites

You need Ruby (>= 3.0) **and a C/C++ toolchain**, since the gem compiles a native
extension.

* **Windows**: install Ruby with DevKit, e.g. `winget install
  RubyInstallerTeam.RubyWithDevKit`, then run `ridk install` (choose the MSYS2
  base + toolchain) and open a new terminal.
* **macOS**: `brew install ruby` (Xcode command-line tools provide the compiler).
* **Linux**: install Ruby plus `build-essential` (or your distro's equivalent).

Verify with `ruby -v` and `gem -v`.

### Build

```sh
bin/build            # macOS / Linux
bin/build.ps1        # Windows (PowerShell)
```

This installs dependencies, compiles the native extension, and packages the gem
into `pkg/palettegen-<version>.gem`.

Other common tasks:

```sh
bundle exec rake            # compile the extension and run the tests
bundle exec rake compile    # just compile the native extension
gem install pkg/palettegen-<version>.gem   # install the built gem locally
```

The C++ core (`ext/palettegen/palettegen.hpp`, `kmeans.cpp`, `image_io.cpp`) has
no Ruby dependency. `palettegen_ext.cpp` is the only Ruby-facing file.

## License

MIT. Bundles [stb_image / stb_image_write](https://github.com/nothings/stb) (public domain / MIT).
