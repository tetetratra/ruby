require_relative './parse_patern_lang.rb'

# RECENT のテストケースがランダムな並びなのは，繰り返しを渡すとマッチの際の計算量が肥大化してしまうため
tests_str = <<"TESTS"
#{Array.new(50) { ["a#{rand(10)}", '=>', "b#{rand(10)}", '=>', "c#{rand(10)}", '->', "d#{rand(10)}", '->', "e#{rand(10)}"].join }.join('->')}
/RECENT/d
d s x 50
#{25.times.flat_map { |i| [i*5 + 125, i*5 + 126] }.join("\n")}
---
x=>y=>a=>b=>a=>b=>z=>w
/y~z/k
k s x 2
0
7
---
x=>y=>a=>b=>a=>b=>z=>w
/y~z/a/k
k s x 2
3
5
---
a=>_=>b
/./d
d s x 2
0
2
---
a=>_=>b
/_/d
d s x 1
1
---
x=>o=>_=>_=>_=>o=>y
/o~o/_/d
d s x 3
2
3
4
---
<main>=>cfunc=><top (required)>=>run=>eval=>eval=>define_variable=>scan=>scan=>scan=>scan=>scan=>scan=>scan=>scan
/scan/d
d s x 8
7
8
9
10
11
12
13
14
---
<main>=>cfunc=><top (required)>=>run=>eval=>eval=>define_variable=>scan=>scan=>scan=>scan=>scan=>scan=>scan=>scan
/scan+/t
t s x 8
7
8
9
10
11
12
13
14
---
<main>=>cfunc=><top (required)>=>run=>eval=>eval=>define_variable=>scan=>scan=>scan=>scan=>scan=>scan=>scan=>scan
/scan/t
t s x 8
7
8
9
10
11
12
13
14
---
f=>_=>_=>g
/_+/t_
t s _ 2
1
2
---
a=>b=>a=>b=>a=>b=>a=>b=>b
/b~b/a/t
t s x 2
2
6
---
block in eval->eval=>apply=>eval=>eval->map->block in eval->eval=>apply=>eval=>eval->map
/block-in-eval~eval-/d
d s x 6
1
2
3
7
8
9
---
block in eval->eval=>apply=>eval=>eval->map->block in eval->eval=>apply=>eval=>eval->map
/block-in-eval~eval-/eval apply eval/d
d s x 6
1
2
3
7
8
9
TESTS

tests = tests_str.split(/^---.*\n/).map do |test_str|
  backtrace = test_str.lines[0].chomp
  pattern   = test_str.lines[1].chomp
  expect    = test_str.lines[2..].join.chomp
  [backtrace, pattern, expect]
end

require 'stringio'
pass_cnt = 0
$debug = true

tests.each do |(bt, pat, exp)|
  stdout_old = $stdout.dup
  $stdout = StringIO.new
  result = run(bt, pat).chomp
  debug_info = $stdout.string
  $stdout = stdout_old

  if result == exp.chomp
    pass_cnt += 1
  else
    puts debug_info
    puts '---'
    puts "backtrace: #{bt}"
    puts "pattern: #{pat}"
    puts '--- expect ---'
    puts exp
    puts '--- actual ---'
    puts result
    puts '-' * 50
    puts "passed #{pass_cnt} tests."
    exit
  end
end
puts "passed all tests."
