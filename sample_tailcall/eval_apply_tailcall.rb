class Lisp
  Function = Struct.new(:parameters, :body, :env)

  Frame = Struct.new(:vars, :vals)

  def initialize(code)
    @exp = parse(code)
    @env = setup_environment
  end

  def run
    eval(@exp, @env)
  end

  private

  def parse(str)
    regex = /\(|\)|[\w\d\-+=*%_@^~<>?$&|!]+/
    tokens = str.gsub(/;.*/, '').gsub(/\s+/, ' ').scan(regex).map do |token|
      case token
      when 'true'
        true
      when 'false'
        false
      when /^\d+$/
        token.to_i
      else
        token.to_sym
      end
    end
    parsed = [tokens.shift]
    until tokens.empty?
      parsed <<
      case s = tokens.shift
      when :')'
        poped = [:')']
        until poped in [:'(', *rest, :')']
          poped = [parsed.pop, *poped]
        end
        poped[1..-2]
      else
        s
      end
    end
    parsed.first
  end

  def eval(exp, env)
    case exp
    in Integer
      exp
    in TrueClass| FalseClass
      exp
    in Symbol
      lookup_variable_value(exp, env)
    in [:quote, l]
      l
    in [:define, name, value]
      raise 'Only `(define name value)` format is allowed' if name.is_a?(Array)
      define_variable(
        name,
        eval(value, env),
        env
      )
    in [:lambda, Array => parameters, body]
      Function.new(parameters, body, env)
    in [:if, cond_body, then_body, *_else_body]
      else_body = _else_body[0]
      if eval(cond_body, env)
        eval(then_body, env)
      elsif else_body
        eval(else_body, env)
      else # else句が無い場合
        false
      end
    in [:begin, *rest]
      if rest.size == 1
        eval(rest.first, env)
      else
        eval(rest.first, env)
        eval([:begin, *rest[1..]], env)
      end
    in [fun, *args] # 関数呼び出し
      apply(
        eval(fun, env),
        args.map { |a| eval(a, env) }
      )
    end
  end

  def apply(func, arguments)
    case func
    in Proc # 組み込み関数
      func.call(*arguments)
    in Function
      eval(
        func.body,
        extend_environment(
          func.parameters,
          arguments,
          func.env # 現在のenv
        )
      )
    else
      raise 'Unknown procedure type -- APPLY'
    end
  end

  def extend_environment(vars, vals, base_env)
    raise "Too many or few arguments supplied" unless vars.size == vals.size
    [Frame.new(vars, vals), *base_env]
  end

  def lookup_variable_value(var, env)
    # ---------- lambda lifting 前 ----------
    # env_loop = -> env {
    #   scan = -> (vars, vals) {
    #     if vars.empty?
    #       env_loop.(env[1..])
    #     elsif var == vars.first
    #       vals.first
    #     else
    #       scan.(vars[1..], vals[1..])
    #     end
    #   }
    #   raise 'Unbound variable' if env.empty?
    #   frame = env.first
    #   scan.(frame.vars, frame.vals)
    # }
    # env_loop.(env)

    def env_loop(env,
                 var)
      def scan(vars, vals,
              var, env) # var, env は lambda lifting
        if vars.empty?
          env_loop(env[1..],
                   var)
        elsif var == vars.first
          vals.first
        else
          scan(vars[1..], vals[1..],
              var, env)
        end
      end
      raise "Unbound variable `#{var}`" if env.empty?
      frame = env.first
      scan(frame.vars, frame.vals,
          var, env)
    end
    env_loop(env,
             var)
  end

  def define_variable(var, val, env)
    frame = env.first
    # ---------- lambda lifting 前 ----------
    # scan = -> (vars, vals) {
    #   if vars.empty?
    #     frame.vars = [var, *frame.vars] # !
    #     frame.vals = [val, *frame.vals] # !
    #   elsif var == vars.first
    #     frame.vals = [val, *frame.vals]
    #   else
    #     scan.(vars[1..], vals[1..])
    #   end
    # }

    def scan(vars, vals,
            frame, var, val) # frame, var, val は lambda lifting
      if vars.empty?
        frame.vars = [var, *frame.vars] # !
        frame.vals = [val, *frame.vals] # !
      elsif var == vars.first
        frame.vals = [val, *frame.vals] # !
      else
        scan(vars[1..], vals[1..],
            frame, var, val)
      end
    end
    # scan.(frame.vars, frame.vals) ---------- lambda lifting 前 ----------
    scan(frame.vars, frame.vals,
         frame, var, val)
  end

  Cons = Struct.new(:car, :cdr)
  def setup_environment
    extend_environment(
      %i[car cdr cons null? = < + - * print],
      [
        ->(x){x.car},
        ->(x){x.cdr},
        ->(x,y){Cons.new(x,y)},
        ->(x){x.nil?},
        ->(x,y){x == y},
        ->(x,y){x < y},
        ->(x,y){x + y},
        ->(x,y){x - y},
        ->(x,y){x * y},
        ->(x){puts x}
      ],
      []
    )
  end
end

