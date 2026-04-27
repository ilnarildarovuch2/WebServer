struct ip_tracker_t {
    uint32 ip;
    int32 request_count;
    int64 window_start;
};

export uniform int find_ip_in_tracker(uniform uint32 target_ip,
                                      uniform uint32 ip_list[],
                                      uniform int count)
{
    uniform int stride = sizeof(ip_tracker_t) / sizeof(uint32);

    for (uniform int i = 0; i < count; ++i) {
        uniform uint32 ip = ip_list[i * stride];

        if (ip == target_ip) {
            return i;
        }
    }

    return -1;
}

export uniform int normalize_path_slashes(uniform int8 path[],
                                          uniform int len)
{
    if (len <= 0) return 0;

    uniform int write_pos = 0;
    uniform int prev_slash = 0;

    for (uniform int i = 0; i < len; ++i) {
        uniform int8 c = path[i];

        if (c == 47) {
            if (!prev_slash) {
                path[write_pos++] = (int8)47;
                prev_slash = 1;
            }
        } else {
            path[write_pos++] = c;
            prev_slash = 0;
        }
    }

    return write_pos;
}

export void transform_header_name(uniform int8 name[],
                                  uniform int len)
{
    for (uniform int i = 0; i < len; i += programCount) {

        varying int idx = i + programIndex;
        varying bool valid = idx < len;

        varying int8 c = select(valid, name[idx], (int8)0);

        varying bool is_dash  = (c == (int8)45);
        varying bool is_lower = (c >= (int8)97) & (c <= (int8)122);

        varying int8 res = c;

        res = select(is_dash,  (int8)95, res);
        res = select(is_lower, (int8)(c - (int8)32), res);

        if (any(valid)) {
            name[idx] = select(valid, res, name[idx]);
        }
    }
}
