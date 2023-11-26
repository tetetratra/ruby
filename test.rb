require_relative './kenbikyo.rb'

x = 'X'
y = 'Y'

puts "\n<toplevel>"
frame_toplevel = ControlFramePointer.current!
frame_toplevel.print_frame!

puts "\n<each>"
[100].each do |_i|
  a = 'A'
  b = 'B'
  frame_each = ControlFramePointer.current!
  frame_each.print_frame!
  frame_each.down.print_frame!
end

puts "\n<Klass>"
class Klass
  s = 'S'
  t = 'T'
  frame_klass = ControlFramePointer.current!
  frame_klass.print_frame!
end

