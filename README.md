# mruby-zstd : mruby bindings for zstd the compression library (unofficial)

## HOW TO INSTALL

`build_config.rb` に `gem mgem: "mruby-zstd"` を追加して下さい。

```ruby
MRuby::Build.new do |conf|
  gem mgem: "mruby-zstd"
end
```

## HOW TO USAGE

### 圧縮

```ruby
src = "123456789"
dest = Zstd.encode(src)
```

圧縮レベルを指定したい場合:

```ruby
src = "123456789"
complevel = 15 # 1..22 の範囲で与える。既定値は nil で、1 と等価
dest = Zstd.encode(src, level: complevel)
```

### 伸長

```ruby
zstdseq = ... # zstd'd string by Zstd.encode
              # OR .zst file data by zstd-cli
dest = Zstd.decode(zstdseq)
```

### ストリーミング圧縮

```ruby
output = AnyObject.new # An object that has ``.<<'' method (e.g. IO, StringIO, or etc.)
Zstd.encode(output) do |zstd|
  zstd << "abcdefg"
  zstd << "123456789" * 99
end
```

### ストリーミング伸長

```ruby
input = AnyObject.new # An object that has ``.read'' method (e.g. IO, StringIO, or etc.)
Zstd.decode(input) do |zstd|
  zstd.read(20)
  buf = ""
  zstd.read(5, buf)
  zstd.read(10, buf)
  zstd.read(nil, buf)
end
```


## build_config.rb

### ``ZSTD_LEGACY_SUPPORT``

``build_config.rb`` で ``ZSTD_LEGACY_SUPPORT`` を定義することによって、zstd-1.0 以前のデータ形式を伸長できるようになります。

```ruby:build_config.rb
MRuby::Build.new("host") do |conf|
  conf.cc.defines << "ZSTD_LEGACY_SUPPORT"

  ...
end
```

### ``MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE``

``build_config.rb`` で ``MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE`` を定義することによって、段階的なメモリ拡張サイズを指定することが出来ます。

```ruby:build_config.rb
MRuby::Build.new("host") do |conf|
  conf.cc.defines << "MRUBY_ZSTD_DEFAULT_PARTIAL_SIZE=65536"

  ...
end
```

既定値は、MRB_INT_16 が定義された場合は 4 KiB、それ以外の場合は 1 MiB となっています。


## Specification

  - Product name: [mruby-zstd](https://github.com/dearblue/mruby-zstd)
  - Version: 0.2.3
  - Product quality: PROTOTYPE
  - Author: [dearblue](https://github.com/dearblue)
  - Report issue to: <https://github.com/dearblue/mruby-zstd/issues>
  - Licensing: [2 clause BSD License](LICENSE)
  - Dependency external mrbgems: (NONE)
  - Bundled C libraries (git-submodule'd):
      - [zstd](https://github.com/facebook/zstd)
        [version 1.3.8](https://github.com/facebook/zstd/tree/v1.3.8)
        under [3 clause BSD License](https://github.com/facebook/zstd/blob/v1.3.8/LICENSE)
        by [Facebook](https://github.com/facebook)
