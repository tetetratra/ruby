class Pointer
  attr_reader :addr

  SIZE = nil
  def self.size = SIZE

  def initialize(addr)
    raise TypeError unless addr.class == Integer

    @addr = addr
  end

  def inspect = "#<#{self.class} @addr=#{addr_inspect}>"
  def addr_inspect = @addr.to_s(16)

  alias to_s inspect

  def +(n)
    raise NotImplementedError if size.nil?

    self.class.new(@addr + size * n)
  end

  def -(n)
    raise NotImplementedError if size.nil?

    self.class.new(@addr - size * n)
  end

  def ==(other)
    @addr == other.addr
  end
end

class ValuePointer < Pointer
  SIZE = 8
  def size = SIZE
end

class ControlFramePointer < Pointer
  SIZE = 64
  def size = SIZE

  def up = self - 1
  def down = self + 1

  def print_frame!
    puts '[[frame description]]'
    puts '    frame:'
    puts frame_description.lines.map { "        #{_1}" }.join
    puts '    iseq:' if iseq
    puts iseq_description.lines.map { "        #{_1}" }.join if iseq
    puts '    sp:'
    ((sp.addr - bp.addr) / ValuePointer::SIZE).times do |i|
      puts "        #{value_description(sp - i)}"
    end
    puts '    ep:'
    (3 + local_size).times do |i|
      puts "        #{value_description(ep - i)}"
    end
  end

  private

  def local_size = iseq&.local_size.to_i

  def bp = down.sp + local_size + 3 # 参考: vm_base_ptr

  def frame_description
    <<~DESC
      pc: #{pc.addr_inspect} (program counter)
      sp: #{sp.addr_inspect} (stack pointer)
      bp: #{bp.addr_inspect} (base pointer)
      ep: #{ep.addr_inspect} (environment pointer)
      self: #{send(:self).inspect}
      iseq: #{iseq.inspect}
    DESC
  end

  def iseq_description
    <<~DESC
      label: #{iseq.label}
      type: #{iseq.type}
      local_size: #{iseq.local_size}
      locals: #{iseq.locals}
      args: #{iseq.args}
    DESC
  end

  def value_description(value_pointer)
    body = case value_pointer
           when       ep then "{{ frame_type: #{frame_type.inspect}. frame_flags: #{frame_flags}. env_flags: #{env_flags} }}"
           when (ep - 1) then "{{ block_handler_type: #{block_handler_type.inspect} }}" # specval (block handler), rb_block_handler_type
           when (ep - 2) then "{{ method_entry_or_cref: TODO }}"
           else value_pointer.to_rb.inspect
           end
    "#{value_pointer.addr_inspect}: #{body}"
  end
end

class RubyVM::InstructionSequence
  def magic = to_a[0]
  def major_version = to_a[1]
  def minor_version = to_a[2]
  def format_type = to_a[3]
  def misc = to_a[4]
  def arg_size = misc[:arg_size]
  def local_size = misc[:local_size]
  def stack_max = misc[:stack_max]
  def label = to_a[5]
  def path = to_a[6]
  def absolute_path = to_a[7]
  def first_lineno = to_a[8]
  def type = to_a[9]
  def locals = to_a[10]
  def args = to_a[11]
  def catch_table = to_a[12]
  def bytecode = to_a[13]

  def bytecode=(bytecode)
    raise NotImplementedError
  end
end
