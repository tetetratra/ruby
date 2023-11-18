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
  # p methods(false) # => []
  # p instance_methods(false) # => [:to_rb]

  SIZE = 8
  def size = SIZE
end

class ControlFramePointer < Pointer
  # p methods(false) # => [:current!]
  # p instance_methods(false) # => [:frame_flags, :self, :pc, :sp, :ep, :iseq, :frame_type]

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
  # p methods(false) # => []
  # p instance_methods(false) # => []
end

class RubyVM::InstructionSequence
  def bytecode = to_a[13]

  def bytecode=(bytecode)
    raise NotImplementedError
  end
end
