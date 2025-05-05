MRuby::Lockfile.disable rescue nil

builddir = File.join(Dir.pwd, "build")

MRuby::Build.new("host", builddir) do |conf|
  toolchain :clang

  enable_debug
  enable_test
  compilers.each do |cc|
    cc.defines << "MRB_STR_LENGTH_MAX=20000000"
    cc.include_paths << "/usr/local/include"
  end
  linker.library_paths << "/usr/local/lib"

  gem core: "mruby-io"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end

MRuby::Build.new("host-nan32-c++abi", builddir) do |conf|
  toolchain :clang

  compilers.each do |cc|
    cc.defines << "MRB_NAN_BOXING"
    cc.defines << "MRB_STR_LENGTH_MAX=20000000"
    cc.defines << "ZSTD_LEGACY_SUPPORT"
    cc.include_paths << "/usr/local/include"
  end
  linker.library_paths << "/usr/local/lib"

  enable_cxx_abi
  enable_debug
  enable_test

  gem core: "mruby-io"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "." do
    enable_zstd "local"
  end
end
