# frozen_string_literal: true

require "mkmf"

# Vendored single-header libraries (stb_image / stb_image_write).
$INCFLAGS << " -I$(srcdir)/vendor"

if RbConfig::CONFIG["host_os"] =~ /mswin/ # MSVC; RubyInstaller (MinGW) uses the else branch
  $CXXFLAGS << " /std:c++17 /O2 /EHsc"
else
  $CXXFLAGS << " -std=c++17 -O2"
end

have_func("rb_ext_ractor_safe", "ruby.h")

# Optional WebP support. Opt out with: gem install palettegen -- --without-webp
if with_config("webp", true) &&
   have_header("webp/decode.h") &&
   have_library("webp", "WebPDecodeRGBAInto", "webp/decode.h")
  $defs << "-DPALETTEGEN_HAVE_WEBP"
  message "palettegen: WebP support enabled\n"
else
  message "palettegen: WebP support disabled (install libwebp headers to enable)\n"
end

create_makefile("palettegen/palettegen")
