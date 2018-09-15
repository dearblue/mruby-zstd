module MrubyZstdInternals
  refine Array do
    def configure_defined?(d)
      flatten.any? { |x| x.partition("=")[0] == d }
    end
  end
end

using MrubyZstdInternals

MRuby::Gem::Specification.new("mruby-zstd") do |s|
  s.summary  = "mruby bindings for zstd the data compression library (unofficial)"
  s.version = File.read(File.join(File.dirname(__FILE__), "README.md")).scan(/^\s*[\-\*] version:\s*(\d+(?:\.\d+)+)/i).flatten[-1]
  s.license  = "BSD-2-Clause"
  s.author   = "dearblue"
  s.homepage = "https://github.com/dearblue/mruby-zstd"

  add_dependency "mruby-string-ext"
  add_dependency "mruby-error"

  unless cc.defines.configure_defined?("MRB_INT16") ||
         cc.defines.configure_defined?("MRUBY_ZSTD_TEST_WITHOUT_IO")
    add_test_dependency "mruby-io"
  end

  if cc.command =~ /\b(?:g?cc|clang)\d*\b/
    cc.flags <<
      "-Wno-shift-negative-value" <<
      "-Wno-shift-count-negative" <<
      "-Wno-shift-count-overflow" <<
      "-Wno-missing-braces"
  end

  dirp = dir.gsub(/[\[\]\{\}\,]/) { |m| "\\#{m}" }
  files = "contrib/zstd/lib/{common,compress,decompress,dictBuilder}/**/*.c"
  objs.concat(Dir.glob(File.join(dirp, files)).map { |f|
    next nil unless File.file? f
    objfile f.relative_path_from(dir).pathmap("#{build_dir}/%X")
  }.compact)

  cc.include_paths.insert 0,
    File.join(dir, "contrib/zstd/lib"),
    File.join(dir, "contrib/zstd/lib/common"),
    File.join(dir, "contrib/zstd/lib/compress"),
    File.join(dir, "contrib/zstd/lib/dictBuilder")

  if cc.defines.configure_defined?("ZSTD_LEGACY_SUPPORT")
    dirp = dir.gsub(/[\[\]\{\}\,]/) { |m| "\\#{m}" }
    files = "contrib/zstd/lib/legacy/**/*.c"
    objs.concat(Dir.glob(File.join(dirp, files)).map { |f|
      next nil unless File.file? f
      objfile f.relative_path_from(dir).pathmap("#{build_dir}/%X")
    }.compact)

    cc.include_paths.insert 0,
      File.join(dir, "contrib/zstd/lib/legacy")
  end
end
