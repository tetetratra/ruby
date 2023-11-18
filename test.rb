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
  # CRuby側に定義がある関数
  # #to_rb

  SIZE = 8
  def size = SIZE
end

class ControlFramePointer < Pointer
  # CRuby側に定義がある関数
  # .current!
  # #self
  # #sp
  # #pc
  # #ep
  # #frame_type

  SIZE = 64
  def size = SIZE

  def up = self - 1
  def down = self + 1

  def print_frame!
    ((sp.ptr - down.sp.ptr) / ValuePointer::SIZE).times do |i|
      value = sp - i

      print '%20s' % ptr_description(value)
      print "#{ptr_inspect}: "
      if ep.ptr == value.ptr # FIXME: topのフレームだとepが変なところにあるからバグる
        print "(#{frame_type}) #{frame_flag}"
      elsif (ep - 1).ptr == value.ptr
        print "(#{frame_type})"
      elsif (ep - 2).ptr == value.ptr
        print "(#{frame_type})"
      else
        print value.to_rb.inspect
      end
      puts
    end
  end

  private

  def ptr_description(value)
    case value.ptr
    when sp.ptr then "(#{ptr_inspect}) sp --> "
    when ep.ptr then "(#{ptr_inspect}) ep --> "
    else ''
    end
  end
end

x = 123
y = 456
cfp = ControlFramePointer.current!
# cfp.print_frame! # topのフレームはepがおかしいのかもしれぬ

p cfp
p cfp.self #=> main
p (cfp.ep - 3).to_rb #=> #<ControlFramePointer @ptr=ffffb562af90> (cfpローカル変数)
p (cfp.ep - 4).to_rb #=> 456
p (cfp.ep - 5).to_rb #=> 123
puts
[1].each do
  a = 777
  b = 888
  c = 999
  cfp2 = ControlFramePointer.current!
  p (cfp2.ep - 4).to_rb
  p (cfp2.ep - 5).to_rb
  p (cfp2.ep - 6).to_rb
  cfp2.print_frame!
  cfp2.down.print_frame!
  # cfp2.down.down.print_frame!
end

