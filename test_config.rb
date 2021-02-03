MRuby::Lockfile.disable rescue nil

MRuby::Build.new do |conf|
  toolchain :clang

  conf.build_dir = "host32"

  enable_debug
  enable_test

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end

MRuby::Build.new("host32-c++") do |conf|
  toolchain :clang

  conf.build_dir = conf.name

  enable_debug
  enable_test
  enable_cxx_abi

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end if false

MRuby::Build.new("host32-with-zstdlegacy") do |conf|
  toolchain :clang

  conf.build_dir = conf.name

  cc.defines << "ZSTD_LEGACY_SUPPORT"

  enable_debug
  enable_test

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end

MRuby::Build.new("host64") do |conf|
  toolchain :clang

  conf.build_dir = conf.name

  cc.defines << "MRB_INT64"

  enable_debug
  enable_test

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end if false

MRuby::Build.new("host-nan32") do |conf|
  toolchain :clang

  conf.build_dir = conf.name

  cc.defines << "MRB_NAN_BOXING"

  enable_debug
  enable_test

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end if false

MRuby::Build.new("host-word32") do |conf|
  toolchain :clang

  conf.build_dir = conf.name

  cc.defines << "MRB_WORD_BOXING"

  enable_debug
  enable_test

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end if false

MRuby::Build.new("host-word64") do |conf|
  toolchain :clang

  conf.build_dir = conf.name

  cc.defines << %w(MRB_WORD_BOXING MRB_INT64)

  enable_debug
  enable_test

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end if false
