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

macros = {
  'REECNT' => '.{10}$',
  'BEGINNING' => '.{10}$',
  '~' => '.*?'
}

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

def exec(codes, string, from = 0)
  # sp: 0,1,2... から始めることで、前方部分一致をサポート
  init_vm = (from..(string.size-1)).map { |i| { sp: i, pc: 0, sp_from: i } }.reverse

  (0..).reduce(init_vm) do |vm, _|
    break if vm.empty?

    vm => [*, { sp:, pc:, sp_from: }] # sp: string pointer
    code = codes[pc]

    if $debug
      p(vm, code) && sleep(1) && puts
    end

    case code
    when /^char (.*)/
      word = string[sp]
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
      { range: range, string: string[range] }
    end
  end.to_a.compact
end

def filter(string, patterns)
  init_range = 0..(string.size - 1)
  patterns.reduce([{ range: init_range, string: string }]) do |ss, filter|
    ss.flat_map do |s|
      pattern = parse(filter)
      p pattern if $DEBUG
      bytecode = compile(pattern)
      p bytecode if $DEBUG
      scan(bytecode, s[:string]).map do
        { range: _1[:range].shift(s[:range].begin), string: _1[:string] }
      end
    end
  end
end

def run(string_raw, pattern_exp)
  string = string_raw.split
  pattern_exp.split('/') => [_discard_empty, *patterns, cmd]

  filtered_ranges = filter(string, patterns)

  s = "#{cmd}\n"
  case cmd
  when 'd'
    s += filtered_ranges.flat_map do |filtered_range|
      [*filtered_range[:range]]
    end.join("\n")
    s
  when 'k'
    all = [*0..(string.size - 1)]
    filtered_ranges.each do |filtered_range|
      all -= [*filtered_range[:range]]
    end
    s += all.join("\n")
    s
  when 't'
    s += filtered_ranges.map { |filtered_range|
      format('%d %d', filtered_range[:range].begin, filtered_range[:range].end)
    }
    s
  end
rescue => e
  STDERR.puts e.inspect
  ''
end

if $PROGRAM_NAME == __FILE__
  $DEBUG = true
  puts run(ARGV[0], ARGV[1])
end
