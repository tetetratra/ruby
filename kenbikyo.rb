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

  private

  def local_size = iseq&.local_size.to_i

  def bp = down.sp + local_size + 3 + (iseq&.type == :method ? 1 : 0) # 参考: vm_base_ptr
end

module ControlFramePointer::PrettyPrint
  def pretty_print(pp)
    pp.object_group(self) do
      pp.group(2) do
        pp.breakable
        pp.group(2, 'frame: {', '}') do
          pp.breakable
          pp.text "program_counter: #{pc.addr_inspect}"
          pp.comma_breakable
          pp.text "stack_pointer: #{sp.addr_inspect}"
          pp.comma_breakable
          pp.text "base_pointer: #{bp.addr_inspect}"
          pp.comma_breakable
          pp.text "environment_pointer: #{ep.addr_inspect}"
          pp.comma_breakable
          pp.text "self: #{send(:self).inspect}"
          pp.comma_breakable
          pp.text "iseq: #{iseq.inspect}"
        end

        pp.breakable
        pp.group(2, 'iseq: {', '}') do
          next unless iseq # breakだと閉じカッコが付かないのでnext

          pp.breakable
          pp.text "label: #{iseq.label}"
          pp.comma_breakable
          pp.text "type: #{iseq.type}"
          pp.comma_breakable
          pp.text "local_size: #{iseq.local_size}"
          pp.comma_breakable
          pp.text "locals: #{iseq.locals}"
          pp.comma_breakable
          pp.text "args: #{iseq.args}"
        end

        pp.breakable
        pp.group(2, 'sp: [', ']') do
          pp.breakable
          stack_value_size = (sp.addr - bp.addr) / ValuePointer::SIZE
          stack_value_size.times do |i|
            pp.text "#{value_description(sp - i)}"
            pp.comma_breakable unless i == stack_value_size - 1
          end
        end

        pp.breakable
        pp.group(2, 'ep: [', ']') do
          pp.breakable
          env_value_size = 3 + local_size
          env_value_size.times do |i|
            pp.text "#{value_description(ep - i)}"
            pp.comma_breakable unless i == env_value_size - 1
          end
        end
      end
    end
  end

  private

  def value_description(value_pointer)
    body = case value_pointer
           when       ep then "(env_data: flags) frame_type: #{frame_type.inspect}, frame_flags: #{frame_flags}, env_flags: #{env_flags}"
           when (ep - 1) then "(env_data: specval) block_handler_type: #{block_handler_type.inspect}"
           when (ep - 2) then "(env_data: me_or_cref) TODO"
           else value_pointer.to_rb.inspect
           end
    "#{value_pointer.addr_inspect}: #{body}"
  end
end

class ControlFramePointer
  include ControlFramePointer::PrettyPrint
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
