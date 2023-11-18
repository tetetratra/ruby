require_relative './kenbikyo.rb'

x = 'x'
y = 'y'
frame_toplevel = ControlFramePointer.current!

puts
puts '<toplevel>'
puts "frame_toplevel.self.inspect: #{frame_toplevel.self.inspect}"
puts "frame_toplevel.iseq.inspect: #{frame_toplevel.iseq.inspect}"
puts "(frame_toplevel.ep - 3).to_rb.inspect: #{(frame_toplevel.ep - 3).to_rb.inspect}"
puts "(frame_toplevel.ep - 4).to_rb.inspect: #{(frame_toplevel.ep - 4).to_rb.inspect}"
puts "(frame_toplevel.ep - 5).to_rb.inspect: #{(frame_toplevel.ep - 5).to_rb.inspect}"
[100].each do |_i|
  puts
  puts '<each>'
  a = 'a'
  b = 'b'
  frame_each = ControlFramePointer.current!
  puts "frame_each.self.inspect: #{frame_each.self.inspect}"
  puts "frame_each.iseq.inspect: #{frame_each.iseq.inspect}"
  puts "(frame_each.ep - 3).to_rb.inspect: #{(frame_each.ep - 3).to_rb.inspect}"
  puts "(frame_each.ep - 4).to_rb.inspect: #{(frame_each.ep - 4).to_rb.inspect}"
  puts "(frame_each.ep - 5).to_rb.inspect: #{(frame_each.ep - 5).to_rb.inspect}"
  puts "(frame_each.ep - 6).to_rb.inspect: #{(frame_each.ep - 6).to_rb.inspect}"
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
  puts "(frame_klass.ep - 3).to_rb.inspect: #{(frame_klass.ep - 3).to_rb.inspect}"
  puts "(frame_klass.ep - 4).to_rb.inspect: #{(frame_klass.ep - 4).to_rb.inspect}"
  puts "(frame_klass.ep - 5).to_rb.inspect: #{(frame_klass.ep - 5).to_rb.inspect}"
  frame_klass.print_frame!
end
