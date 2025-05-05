MRuby::Lockfile.disable rescue nil

MRuby::Build.new do |conf|
  toolchain :clang

  conf.build_dir = "host32"

  enable_debug
  enable_test
  cc.defines << "MRB_STR_LENGTH_MAX=200000000"

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
  cc.defines << "MRB_STR_LENGTH_MAX=200000000"
  cxx.defines << "MRB_STR_LENGTH_MAX=200000000"

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end if false

MRuby::Build.new("host32-with-zstdlegacy") do |conf|
  toolchain :clang

  conf.build_dir = conf.name

  cc.defines << "ZSTD_LEGACY_SUPPORT"
  cc.defines << "MRB_STR_LENGTH_MAX=200000000"

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
  cc.defines << "MRB_STR_LENGTH_MAX=200000000"

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
  cc.defines << "MRB_STR_LENGTH_MAX=200000000"

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
  cc.defines << "MRB_STR_LENGTH_MAX=200000000"

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
  cc.defines << "MRB_STR_LENGTH_MAX=200000000"

  enable_debug
  enable_test

  gem core: "mruby-print"
  gem core: "mruby-bin-mrbc"
  gem core: "mruby-bin-mruby"
  gem "."
end if false
