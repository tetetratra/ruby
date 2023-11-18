class Pointer
  attr_reader :ptr

  SIZE = nil
  def size = SIZE

  def initialize(ptr)
    @ptr = ptr
  end

  # トップクラスのepのみ値がおかしいから、initialize時に&ffffffffのようなことはできない(しない)
  def inspect = "#<#{self.class} @ptr=#{ptr_inspect}>"
  def ptr_inspect = @ptr.to_s(16)[4..]

  alias to_s inspect

  def +(n)
    raise NotImplementedError if size.nil?

    self.class.new(@ptr + size * n)
  end

  def -(n)
    raise NotImplementedError if size.nil?

    self.class.new(@ptr - size * n)
  end
end

class ValuePointer < Pointer
  # [CRuby側に定義がある関数]
  # #to_rb

  SIZE = 8
  def size = SIZE
end

class ControlFramePointer < Pointer
  # [CRuby側に定義がある関数]
  # .current!
  # #self
  # #sp
  # #pc
  # #ep
  # #iseq
  # #frame_type

  SIZE = 64
  def size = SIZE

  def up = self - 1
  def down = self + 1

  def print_frame!
    ((sp.ptr - down.sp.ptr) / ValuePointer::SIZE).times do |i|
      value = sp - i
      puts "| #{pointer_description(value)} #{ptr_inspect}: #{value_description(value)}"
    end
  end

  private

  def pointer_description(value)
    format('%16s', case value.ptr
                   when sp.ptr then "#{ptr_inspect}: sp -->"
                   when ep.ptr then "#{ptr_inspect}: ep -->"
                   else ''
                   end)
  end

  def value_description(value)
    # FIXME: topのフレームだとepが変なところにあるからバグる
    case value.ptr
    when       ep.ptr then "(#{frame_type}) #{frame_flags.join(', ')}"
    when (ep - 1).ptr then "(#{frame_type})"
    when (ep - 2).ptr then "(#{frame_type})"
    else value.to_rb.inspect
    end
  end
end

class IseqPointer < Pointer
  # [CRuby側に定義がある関数]
end

class RubyVM::InstructionSequence
  def bytecode = to_a[13]
end

x = 'x'
y = 'y'
frame_toplevel = ControlFramePointer.current!

puts
puts '<toplevel>'
puts "frame_toplevel.self.inspect: #{frame_toplevel.self.inspect}"
puts "frame_toplevel.iseq.inspect: #{frame_toplevel.iseq.inspect}"
puts "(frame_toplevel.ep - 3).to_rb: #{(frame_toplevel.ep - 3).to_rb.inspect}"
puts "(frame_toplevel.ep - 4).to_rb: #{(frame_toplevel.ep - 4).to_rb.inspect}"
puts "(frame_toplevel.ep - 5).to_rb: #{(frame_toplevel.ep - 5).to_rb.inspect}"
[1].each do
  puts
  puts '<each>'
  a = 'a'
  b = 'b'
  frame_each = ControlFramePointer.current!
  puts "frame_each.self.inspect: #{frame_each.self.inspect}"
  puts "frame_each.iseq.inspect: #{frame_each.iseq.inspect}"
  puts "(frame_each.ep - 3).to_rb: #{(frame_each.ep - 3).to_rb.inspect}"
  puts "(frame_each.ep - 4).to_rb: #{(frame_each.ep - 4).to_rb.inspect}"
  puts "(frame_each.ep - 5).to_rb: #{(frame_each.ep - 5).to_rb.inspect}"
  frame_each.print_frame!
  frame_each.down.print_frame!
end

class Klass
  puts
  puts '<Klass>'
  s = 's'
  t = 't'
  frame_klass = ControlFramePointer.current!
  puts "frame_klass.self.inspect: #{frame_klass.self.inspect}"
  puts "frame_klass.iseq.inspect: #{frame_klass.iseq.inspect}"
  puts "(frame_klass.ep - 3).to_rb: #{(frame_klass.ep - 3).to_rb.inspect}"
  puts "(frame_klass.ep - 4).to_rb: #{(frame_klass.ep - 4).to_rb.inspect}"
  puts "(frame_klass.ep - 5).to_rb: #{(frame_klass.ep - 5).to_rb.inspect}"
  frame_klass.print_frame!
end
