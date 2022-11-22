RubyVM::InstructionSequence.compile_option = { tailcall_optimization: true }

def a()
  puts '---'
  puts b(5)
  puts '---'
  puts b(5)
end

RubyVM::InstructionSequence.compile_file(
  File.expand_path('tailcall.rb', __dir__)
).eval

def c()
  caller_locations(0)
end

puts a()

