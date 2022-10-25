RubyVM::InstructionSequence.compile_option = { tailcall_optimization: true }
file_path = __FILE__.sub(/\.rb$/, '_tailcall.rb')
RubyVM::InstructionSequence.compile(
  File.open(file_path).read,
  file_path
).eval

code = <<~LISP
  (define sum (lambda (s n)
                      (if (= n 0)
                          x
                          (sum (+ s n) (- n 1)))))
  (print (sum 0 500))
LISP
Lisp.new("(begin #{code})").run
