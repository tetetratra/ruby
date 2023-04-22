$with_self_methods = []

class WithProxy
  def initialize(elf)
    @elf = elf
  end

  def method_missing(method, *args, &block)
    with_self_method = :"withself_#{method}"
    $with_self_methods << with_self_method

    eval <<-EOS
      def @elf.#{with_self_method}(*a, &b)
        #{method}(*a) { |e| b.(e) }
      end
    EOS

    ret = @elf.send(with_self_method, *args, &block)
    $with_self_methods.pop
    ret
  end

  TP_CALL = TracePoint.trace(:call) do |tp|
    next unless $with_self_methods.any?(tp.method_id)

    tracepoint_call
    $fire_b_call_hook = true
  end

  TP_RETURN = TracePoint.trace(:return) do |tp|
    next unless $with_self_methods.any?(tp.method_id)

    $fire_b_call_hook = false
  end

  TP_B_CALL = TracePoint.trace(:b_call) do |tp|
    next unless $fire_b_call_hook

    tracepoint_b_call
  end
end

class Object
  def with
    WithProxy.new(self)
  end
end

