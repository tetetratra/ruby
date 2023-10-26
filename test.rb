class Pointer
  attr_reader :ptr

  SIZE = nil
  def size = SIZE

  def initialize(ptr)
    @ptr = ptr
  end

  # トップクラスのepのみ値がおかしいから、initialize時に&ffffffffのようなことはできない(しない)
  def inspect = "#<#{self.class} @ptr=#{@ptr.to_s(16)}>"

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
  SIZE = 8
  def size = SIZE
end

class ControlFramePointer < Pointer
  SIZE = 64
  def size = SIZE

  def print_frame!(x = nil)
    # spからcfpのivの個数分下までをprint
    puts " /\n| ptr: #{@ptr.to_s(16)}\n| sp: #{sp}\n| ep: #{ep}\n| stack:"
    ((sp.ptr - ((self + 1).sp).ptr) / ValuePointer::SIZE).times do |i| # (今のep - 3 == 1個前のsp) みたいなことがあるから、spの差を使っている今のやりかたではよくない
      v = sp - i
      puts  "|   #{v.ptr.to_s(16)} : #{[1,2,3].any?(i) ? '-' : v.to_rb.inspect}"
      # type <- ep
      # special
      # cref_or_me
    end
    puts ' \\'
  end
end

x = 123
y = 456
cfp = ControlFramePointer.current!
cfp.print_frame! # topのフレームはepがおかしいのかもしれぬ

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
  (cfp2 + 1).print_frame!
  (cfp2 + 2).print_frame!
end

