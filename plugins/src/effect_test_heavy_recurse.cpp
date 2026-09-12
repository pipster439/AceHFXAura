
        template<int N> struct RecurseLoop {
            enum { val = RecurseLoop<N-1>::val + RecurseLoop<N-2>::val };
        };
        template struct RecurseLoop<10000>;
        