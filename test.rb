require_relative './kenbikyo.rb'
require 'pp'

x = 'X'
y = 'Y'

puts "\n<toplevel>"
frame_toplevel = ControlFramePointer.current!
pp frame_toplevel

puts "\n<each>"
[100].each do |_i|
  a = 'A'
  b = 'B'
  frame_each = ControlFramePointer.current!
  pp frame_each
  pp frame_each.down
end

puts "\n<Klass>"
class Klass
  s = 'S'
  t = 'T'
  frame_klass = ControlFramePointer.current!
  pp frame_klass
end

