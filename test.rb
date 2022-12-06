def a()
  b(5)
end

def d()
  raise
end

RubyVM::InstructionSequence.compile_option = { tailcall_optimization: true }
RubyVM::InstructionSequence.compile(
  File.open(File.expand_path('tailcall.rb', __dir__)).read,
  'tailcall.rb'
).eval

a()

