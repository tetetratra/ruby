RubyVM::InstructionSequence.compile_option = { tailcall_optimization: true }
file_path = __FILE__.sub(/\.rb$/, '_tailcall.rb')
RubyVM::InstructionSequence.compile(
  File.open(file_path).read,
  file_path
).eval

f(950)
