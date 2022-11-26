RubyVM::InstructionSequence.compile_option = { tailcall_optimization: true }

def a()
  b1(10)
end

RubyVM::InstructionSequence.compile_file(
  File.expand_path('tailcall.rb', __dir__)
).eval

def c()
  caller_locations(0)
end

puts
puts
puts
puts a()
puts
puts
puts

