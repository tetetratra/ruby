# def syakutori_loop(a, bound)
#   right = 0
#   left = 0
#   sum = 0
#   result = []
#   while left < a.size
#     while right < a.size && sum + a[right] <= bound
#       sum += a[right]
#       right += 1
#     end
#     result << [left, right]
#     sum -= a[left]
#     left += 1
#   end
#   result
# end
# a = [4,6,7,8,1,2,110,2,4,12,3,9]
# bound = 25
# pp syakutori_loop(a, bound)

def syaku_l(l, r, s, result)
  if l < $arr.size
    syaku_r(l + 1, r, s - $arr[l], [*result, [l, r]])
  else
    puts caller_locations(0)
    result
  end
end

def syaku_r(l, r, s, result)
  if r < $arr.size && s + $arr[r] <= $border
    syaku_r(l, r + 1, s + $arr[r], result)
  else
    syaku_l(l, r, s, result)
  end
end

$arr = Array.new(10000) { rand(10) }
# $arr = (大きな整数配列)
$border = 1000
syaku_r(0, 0, 0, [])


