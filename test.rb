$tcl_filter = [
  { method: 'a', filter: :keep_begin_and_end, keep_size: 2 }
]

def a()
  b(5)
end

def d()
  raise
  caller_locations(0)
end

RubyVM::InstructionSequence.compile_option = { tailcall_optimization: true }
RubyVM::InstructionSequence.compile(
  File.open(File.expand_path('tailcall.rb', __dir__)).read,
  'tailcall.rb'
).eval

puts a()

