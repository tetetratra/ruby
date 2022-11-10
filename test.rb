RubyVM::InstructionSequence.compile_option = { tailcall_optimization: true }

def a(n)
  # puts b(n)
  # puts '---'
  puts b(n)
end

RubyVM::InstructionSequence.compile_file(
  File.expand_path('tailcall.rb', __dir__)
).eval

def c()
  caller_locations(0)
end

a(5)

