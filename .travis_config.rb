#ruby

unless Object.const_defined?(:MRUBY_RELEASE_NO)
  if File.read(File.join(MRUBY_ROOT, "README.md")) =~ /\bversion\s*\K(\d+)\.(\d+)\.(\d+)\s+/im
    MRUBY_RELEASE_NO = $1.to_i * 10000 + $2.to_i * 100 + $3.to_i
  else
    warn "mruby version not found! temporary version number is set to 1.0.0"
    MRUBY_RELEASE_NO = 10000
  end
end

{
  "host" => {
    "defines" => "MRB_INT32",
  },
  "host64-with-legacy" => {
    "defines" => ["MRB_INT64", "ZSTD_LEGACY_SUPPORT"],
  },
  "host16" => {
    "defines" => "MRB_INT16",
  }
}.each_pair do |name, c|
  MRuby::Build.new(name) do |conf|
    toolchain :gcc

    conf.build_dir = c["build_dir"] || name

    enable_debug
    enable_test

    cc.defines = [*c["defines"], "_BSD_SOURCE", "MRUBY_ZSTD_TEST_WITHOUT_IO"]
    cc.flags << "-Wall" << "-std=c11" << "-Wno-declaration-after-statement"
    cc.command = "gcc-7"
    cxx.command = "g++-7"

    gem core: "mruby-print"
    gem core: "mruby-bin-mrbc"
    gem File.dirname(__FILE__)
  end
end
