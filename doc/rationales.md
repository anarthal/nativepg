## Why `field_view::data_str()` if you have `field_view::data()`?

When values use the text format (a common option), they are indeed strings of text.
Forcing a `reinterpret_cast` for this common use is not ergonomic.
`data_str()` is more explicit than `data()`. When typing it, the user is
acknowledging that their values are expected as text.
