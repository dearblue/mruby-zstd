module Zstd
  #
  # call-seq:
  #   encode(input_string, level = nil, opts = {}) -> zstd compressed string
  #   encode(output_stream, level = nil, opts = {}) -> instance of Zstd::Encoder
  #   encode(output_stream, level = nil, opts = {}) { |instance of Zstd::Encoder| ... } -> yeald value
  #
  # [input_string (String)]
  #   string object
  #
  # [output_stream (any object)]
  #   Output port for Zstd stream.
  #   Need +.<<+ method.
  #
  # [level (nil or 1..22)]
  #
  # [opts (Hash)]
  #
  #   level (integer OR nil):: compression level (range is 1..22)
  #
  #   dict (string OR nil):: compression with dictionary
  #
  #   windowlog, chainlog, hashlog, searchlog, searchlength, targetlength, strategy (integer OR nil)::
  #     see https://github.com/facebook/zstd/blob/v1.3.0/lib/zstd.h#L403
  #
  #   nocontentsize, nochecksum, nodictid (true, false OR nil)::
  #     see https://github.com/facebook/zstd/blob/v1.3.0/lib/zstd.h#L428
  #
  #   estimatedsize (integer OR nil)::
  #     (streaming compression only) used as hint
  #
  #   pledgedsize (integer OR nil)::
  #     (streaming compression only) used as source size
  #
  def Zstd.encode(port, *args, &block)
    if port.is_a?(String)
      Zstd::Encoder.encode(port, *args)
    else
      Zstd::Encoder.wrap(port, *args, &block)
    end
  end

  #
  # call-seq:
  #   decode(compressed_string, opts = {}) -> zstd compressed string
  #   decode(input_stream, opts = {}) -> instance of Zstd::Decoder
  #   decode(input_stream, opts = {}) { |instance of Zstd::Decoder| ... } -> yeald value
  #
  # [compressed_string (String)]
  #   string object
  #
  # [input_stream (any object)]
  #   Input port for Zstd stream.
  #   Need +.read+ method.
  #
  # [opts (Hash)]
  #
  #   dict (string OR nil):: decompression with dictionary
  #
  def Zstd.decode(port, *args, &block)
    if port.is_a?(String)
      Zstd::Decoder.decode(port, *args)
    else
      Zstd::Decoder.wrap(port, *args, &block)
    end
  end

  module StreamWrapper
    def wrap(*args)
      zstd = new(*args)

      return zstd unless block_given?

      begin
        yield zstd
      ensure
        zstd.close rescue nil
        zstd = nil
      end
    end
  end

  Encoder.extend StreamWrapper
  Decoder.extend StreamWrapper

  #
  # <em>REQUIRED mruby-gems: mruby-io</em>
  #
  # === Example
  #
  #   filename = "sample/data.zst"
  #   data = "abcdefg"
  #   opts = { ... }
  #   Zstd.write(filename, data, opts) # => nil
  #
  def Zstd.write(file, data, *args)
    File.open(file, "wb") do |fd|
      Zstd::Encoder.open(fo, *args) do |zstd|
        buf = ""
        zstd << buf while fd.read(16384, buf)
      end
    end
  end

  #
  # <em>REQUIRED mruby-gems: mruby-io</em>
  #
  # === Example
  #
  #   filename = "sample/data.zst"
  #   Zstd.read(filename) # => data as string
  #
  def Zstd.read(file)
    File.open(file, "rb") do |fo|
      Zstd::Decoder.open(fo, *args) do |zstd|
        zstd.read
      end
    end
  end

  class Encoder
  end

  class Decoder
    def getc
      read(1)
    end

    def getbyte
      ch = getc
      ch ? ch.ord : nil
    end
  end

  Compressor = Encoder
  Decompressor = Decoder
  Uncompressor = Decoder

  class << Encoder
    alias compress encode
  end

  class << Decoder
    alias decompress decode
    alias uncompress decode
  end

  class << Zstd
    alias compress encode
    alias decompress decode
    alias uncompress decode
  end
end
