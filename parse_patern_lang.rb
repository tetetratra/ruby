$debug = false

Plus = Struct.new(:body) do
  def inspect
    "#{body.inspect}+"
  end
end

Mul = Struct.new(:body) do
  def inspect
    "#{body.inspect}*"
  end
end

Hat = Struct.new(:_) do
  def inspect
    '^'
  end
end

Doller = Struct.new(:_) do
  def inspect
    '$'
  end
end

class Range
  def shift(n)
    (self.begin + n)..(self.end + n)
  end
end

MACROS = {
  'REECNT' => '.{10}$',
  'BEGINNING' => '.{10}$',
  '~' => '.*?'
}

Call = Struct.new(:name) do
  def inspect
    "@#{name.inspect}"
  end
end

Tailcall = Struct.new(:name) do
  def inspect
    name.inspect
  end
end

def run(string_raw, pattern_exp)
  string = string_raw.split.map do |s|
    if s.start_with?('@')
      Call.new(s.sub('@', ''))
    else
      Tailcall.new(s)
    end
  end
  p string if $debug
  pattern_exp.split('/') => [_discard_empty, *patterns, cmd]

  range_string_arr = filter(string, patterns)

  s = "#{cmd}\n"
  case cmd
  when 'd'
    s += range_string_arr.map do |(r, _s)|
      r.to_a.join("\n")
    end.join("\n")
    s
  when 'k'
    all = (0..(string.size - 1)).to_a
    range_string_arr.each do |(r, _s)|
      all -= r.to_a
    end
    s += all.join("\n")
    s
  when 't'
    s += range_string_arr.map do |(r, s)|
      r.to_a.zip(s).chunk { |(i, c)| Tailcall === c }
        .select(&:first) # Callは取り除く
        .map(&:last)
        .map { _1.map(&:first) }
        .map { format('%d %d', _1[0], _1[-1]) }.join("\n")
    end.join("\n")
    s
  end
rescue => e
  STDERR.puts e.full_message
  ''
end

def filter(init_string, patterns)
  init_range = 0..(init_string.size - 1)
  patterns.reduce([[init_range, init_string]]) do |range_string_arr, pattern_code|
    range_string_arr.flat_map do |(range, string)|
      pattern_ast = parse(pattern_code)
      p pattern_ast if $debug
      pattern_bytecode = compile(pattern_ast)
      p pattern_bytecode if $debug
      scan(pattern_bytecode, string).map do |(range_result, string_result)|
        [range_result.shift(range.begin), string_result]
      end
    end
  end
end

def parse(pattern_str)
  parsed = pattern_str.scan(%r#\^|\$|\.|[A-Z]+|[a-z_<>]+|\+/d|\*/d|\+|\*|\(|\)|_|~#)
  pattern = []
  until parsed.empty?
    poped = parsed.shift
    case poped
    when ')'
      i = pattern.rindex('(')
      pattern = i.zero? ? [pattern[(i+1)..]] : [*pattern[0..(i-1)], pattern[(i+1)..]]
    when '+'
      pattern << Plus.new(pattern.pop)
    when '*'
      pattern << Mul.new(pattern.pop)
    when '^'
      pattern << Hat.new
    when '$'
      pattern << Doller.new
    else
      pattern << poped
    end
  end
  pattern
end

def compile(pattern)
  compile_r = ->(pat) do
    case pat
    in String
      ["char #{pat}"]
    in Array
      pat.map { |p| compile_r.(p) }.flatten(1)
    in Plus
      compiled = compile_r.(pat.body)
      [
        *compiled,
        "split 1 #{compiled.size + 2}",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    in Mul
      compiled = compile_r.(pat.body)
      [
        "split 1 #{compiled.size + 2}",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    in Hat
      ['hat']
    in Doller
      ['doller']
    end
  end
  [*compile_r.(pattern), 'match']
end

def scan(code, string)
  from = 0
  Enumerator.produce do
    range = exec(code, string, from)
    raise StopIteration if range.nil?

    if range.begin > range.end
      from = range.begin + 1
      nil
    else
      from = range.end + 1
      [range, string[range]]
    end
  end.to_a.compact
end

def exec(codes, string, from = 0)
  # sp: 0,1,2... から始めることで、前方部分一致をサポート
  init_vm = (from..(string.size - 1)).map { |i| { sp: i, pc: 0, sp_from: i } }.reverse

  (0..).reduce(init_vm) do |vm, _|
    break if vm.empty?

    vm => [*, { sp:, pc:, sp_from: }] # sp: string pointer
    code = codes[pc]

    if $debug
      p vm, code
      puts
    end

    case code
    when /^char (.*)/
      word = string[sp].name
      next vm[..-2] if word.nil?

      c = $1
      if word == c || c == '.'
        next [*vm[..-2], { sp: sp + 1, pc: pc + 1, sp_from: }]
      else
        next vm[..-2]
      end
    when /^hat/
      if sp == 0
        next [*vm[..-2], { sp: sp, pc: pc + 1, sp_from: sp_from }]
      else
        next vm[..-2]
      end
    when /^doller/
      if sp == string.size
        next [*vm[..-2], { sp: sp, pc: pc + 1, sp_from: sp_from }]
      else
        next vm[..-2]
      end
    when /^split (-?\d+) (-?\d+)/
      j1 = $1.to_i
      j2 = $2.to_i
      next [
        *vm[..-2],
        { sp: sp, pc: pc + 1,  sp_from: },
        { sp: sp, pc: pc + j2, sp_from: },
        { sp: sp, pc: pc + j1, sp_from: } # 最長一致のため、繰り返す方(j1)を先に試す
      ]
    when /^jump (-?\d+)/
      j = $1.to_i
      next [*vm[..-2], { sp: sp, pc: pc + j, sp_from: }]
    when /^match/
      break sp_from..(sp - 1)
    end
  end
end

if $PROGRAM_NAME == __FILE__
  $debug = true
  puts run(ARGV[0], ARGV[1])
end
