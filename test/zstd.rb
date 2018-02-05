assert("Zstd:one step processing") do
  s = "123456789" * 111
  assert_equal s, Zstd.decode(Zstd.encode(s))
  assert_equal s, Zstd.decode(Zstd.encode(s, level: nil))
  assert_equal s, Zstd.decode(Zstd.encode(s, level: 0))
  assert_equal s, Zstd.decode(Zstd.encode(s, level: -2000))
  assert_equal s, Zstd.decode(Zstd.encode(s, level: 2000))
end

assert("Zstd:one step encoding") do
  assert_raise(TypeError) { Zstd::Encoder.encode(nil) }
  assert_raise(TypeError) { Zstd::Encoder.encode(12345) }

  s = "123456789" * 111

  d = ""
  assert_equal d.object_id, Zstd.encode(s, d).object_id
  assert_equal d.object_id, Zstd.encode(s, 200, d).object_id
  assert_raise(RuntimeError) { Zstd.encode(s, 10) }
end

assert("Zstd:one step decoding") do
  s = "123456789" * 111
  ss = Zstd.encode(s)

  d = ""
  assert_equal d.object_id, Zstd.decode(ss, d).object_id
  assert_equal s.byteslice(0, 20), Zstd.decode(ss, 20, d)
end

assert("Zstd:stream encoding") do
  s0 = "123456789"
  times = 111
  s = "123456789" * 111

  d = ""
  zstd = Zstd::Encoder.new(d)
  times.times { zstd.write s0 }
  zstd.close

  assert_equal s, Zstd.decode(d)
end

assert("Zstd:stream decoding1") do
  s = "123456789" * 111
  ss = Zstd.encode(s)

  zstd = Zstd::Decoder.new(ss)
  assert_equal s, zstd.read
  zstd.close

  true
end

assert("Zstd:stream decoding2") do
  s = "123456789" * 111
  ss = Zstd.encode(s)

  zstd = Zstd::Decoder.new(ss)
  assert_equal s.byteslice(0, 50), zstd.read(50)
  assert_equal s.byteslice(50..-1), zstd.read
  assert_equal nil, zstd.read
  assert_equal "", zstd.read(0)
  zstd.close

  true
end

assert("Zstd:stream decoding3") do
  s = "123456789" * 111
  ss = Zstd.encode(s)

  dest = ""
  zstd = Zstd::Decoder.new(ss)
  assert_equal s.byteslice(0, 50), zstd.read(50, dest)
  assert_equal s.byteslice(50..-1), zstd.read(nil, dest)
  assert_equal nil, zstd.read(nil, dest)
  assert_equal "", zstd.read(0, dest)
  zstd.close

  true
end

assert("Zstd:stream decoding4") do
  s = "123456789" * 111
  ss = Zstd.encode(s)

  dest = ""
  zstd = Zstd::Decoder.new(ss)
  assert_equal dest.object_id, zstd.read(50, dest).object_id
  assert_equal dest.object_id, zstd.read(nil, dest).object_id
  assert_equal dest.object_id, zstd.read(0, dest).object_id
  assert_equal nil.object_id, zstd.read(nil, dest).object_id
  zstd.close

  true
end

assert("Zstd - stream processing (huge)") do
  unless (1 << 28).kind_of?(Integer)
    skip "[mruby is build with MRB_INT16]"
  end

  s = "123456789" * 11111111 + "ABCDEFG"
  d = ""
  Zstd::Encoder.wrap(d, level: 1) do |zstd|
    off = 0
    slicesize = 777777
    while off < s.bytesize
      assert_equal zstd, zstd.write(s.byteslice(off, slicesize))
      off += slicesize
      slicesize = slicesize * 3 + 7
    end
  end

  assert_equal s.hash, Zstd.decode(d, s.bytesize).hash
  assert_equal s.hash, Zstd.decode(d).hash

  Zstd::Decoder.wrap(d) do |zstd|
    off = 0
    slicesize = 3
    while off < s.bytesize
      assert_equal s.byteslice(off, slicesize).hash, zstd.read(slicesize).hash
      off += slicesize
      slicesize = slicesize * 2 + 3
    end

    assert_equal nil.hash, zstd.read(slicesize).hash
  end
end

assert("Zstd:large stream encoding with IO") do
  skip "(without mruby-io)" unless Object.const_defined?(:File)

  File.open("/dev/urandom", "rb") do |src|
    File.open("#SAMPLE.rand.zst", "wb") do |dest|
      Zstd.encode(dest) do |z|
        buf = ""
        300.times { src.read(30000, buf); z << buf }
      end
    end
  end

  ss = File.open("#SAMPLE.rand.zst", "rb") { |f| f.read }
  Zstd.decode(ss)
  true
end

assert("Zstd:large stream decoding with IO") do
  skip "(without mruby-io)" unless Object.const_defined?(:File)

  File.open("#SAMPLE.rand.zst", "rb") do |src|
    Zstd.decode(src) do |z|
      buf = ""
      300.times { z.read(30000, buf) }
      assert_equal nil, z.read
    end
  end

  true
end
