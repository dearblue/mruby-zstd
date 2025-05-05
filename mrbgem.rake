internals = File.join(__dir__, "contrib/mruby-buildconf/bootstrap.rb")
using Module.new { module_eval File.read(internals), internals, 1 }

using Module.new {
  refine Array do
    def configure_defined?(d)
      flatten.any? { |x| x.partition("=")[0] == d }
    end
  end
}

MRuby::Gem::Specification.new("mruby-zstd") do |s|
  s.summary  = "mruby bindings for zstd the data compression library (unofficial)"
  version = File.read(File.join(__dir__, "README.md")).scan(/^\s*[\-\*] version:\s*(\d+(?:\.\d+)+)/i).flatten[-1]
  s.version = version
  s.license  = "BSD-2-Clause"
  s.author   = "dearblue"
  s.homepage = "https://github.com/dearblue/mruby-zstd"

  add_dependency "mruby-string-ext"
  add_dependency "mruby-error"
  add_dependency "mruby-aux", github: "dearblue/mruby-aux"

  unless cc.defines.configure_defined?("MRB_INT16") ||
         cc.defines.configure_defined?("MRUBY_ZSTD_TEST_WITHOUT_IO")
    add_test_dependency "mruby-io"
  end

  configuration_recipe(
    "zstd",
    {
      libraries: %w(zstd),
      code: <<~'CODE'
        #include <zstd.h>
        #include <zdict.h>

        #if ZSTD_VERSION_NUMBER < 10500
        # error NEED zstd-1.5.0 or newer
        #endif

        int
        main(int argc, char *argv[])
        {
          size_t ret = ZDICT_trainFromBuffer(NULL, 0, NULL, NULL, 0);
          (void)ret;

          return 0;
        }
      CODE
    },
    {
      variation: "local",
      standard: false,
      objs: -> {
        dirp = dir.gsub(/[\[\]\{\}\,]/) { |m| "\\#{m}" }
        legacy = ",legacy" if cc.defines.configure_defined?("ZSTD_LEGACY_SUPPORT")
        files = "contrib/zstd/lib/{common,compress,decompress,dictBuilder#{legacy}}/**/*.c"
        Dir.glob(File.join(dirp, files)).map { |f|
          next nil unless File.file? f
          objfile f.relative_path_from(dir).pathmap("#{build_dir}/%X")
        }.compact
      },
      include_paths: -> {
        legacy = File.join(dir, "contrib/zstd/lib/legacy") if cc.defines.configure_defined?("ZSTD_LEGACY_SUPPORT")
        [
          File.join(dir, "contrib/zstd/lib"),
          File.join(dir, "contrib/zstd/lib/common"),
          File.join(dir, "contrib/zstd/lib/compress"),
          File.join(dir, "contrib/zstd/lib/dictBuilder"),
          *legacy
        ]
      }
    },
    abort: true,
    default: true
  )
end
