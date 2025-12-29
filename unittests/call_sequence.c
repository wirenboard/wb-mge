static unsigned call_id = 0;

unsigned call_sequence_get_call_id(void)
{
    call_id++;
    return call_id;
}
