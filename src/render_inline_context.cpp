#include "html.h"
#include "render_item.h"
#include "document.h"
#include "iterators.h"

int litehtml::render_item_inline_context::_render_content(int x, int y, int max_width, bool second_pass, int ret_width)
{
    m_line_boxes.clear();
	m_max_line_width = 0;

    int block_height = 0;

    if (get_predefined_height(block_height))
    {
        m_pos.height = block_height;
    }

    white_space ws = src_el()->css().get_white_space();
    bool skip_spaces = false;
    if (ws == white_space_normal ||
        ws == white_space_nowrap ||
        ws == white_space_pre_line)
    {
        skip_spaces = true;
    }

    bool was_space = false;

    go_inside_inline go_inside_inlines_selector;
    inline_selector select_inlines;
    elements_iterator inlines_iter(true, &go_inside_inlines_selector, &select_inlines);

    inlines_iter.process(shared_from_this(), [&](const std::shared_ptr<render_item>& el, iterator_item_type item_type)
        {
			switch (item_type)
			{
				case iterator_item_type_child:
					{
						// skip spaces to make rendering a bit faster
						if (skip_spaces)
						{
							if (el->src_el()->is_white_space())
							{
								if (was_space)
								{
									el->skip(true);
									return;
								} else
								{
									was_space = true;
								}
							} else
							{
								// skip all spaces after line break
								was_space = el->src_el()->is_break();
							}
						}
						// place element into rendering flow
						place_inline(std::unique_ptr<line_box_item>(new line_box_item(el)), max_width);
					}
					break;

				case iterator_item_type_start_parent:
					{
						el->clear_inline_boxes();
						place_inline(std::unique_ptr<lbi_start>(new lbi_start(el)), max_width);
					}
					break;

				case iterator_item_type_end_parent:
				{
					place_inline(std::unique_ptr<lbi_end>(new lbi_end(el)), max_width);
				}
					break;
			}
        });

    finish_last_box(true, max_width);

    if (!m_line_boxes.empty())
    {
        if (collapse_top_margin())
        {
            int old_top = m_margins.top;
            m_margins.top = std::max(m_line_boxes.front()->top_margin(), m_margins.top);
            if (m_margins.top != old_top)
            {
                update_floats(m_margins.top - old_top, shared_from_this());
            }
        }
        if (collapse_bottom_margin())
        {
            m_margins.bottom = std::max(m_line_boxes.back()->bottom_margin(), m_margins.bottom);
            m_pos.height = m_line_boxes.back()->bottom() - m_line_boxes.back()->bottom_margin();
        }
        else
        {
            m_pos.height = m_line_boxes.back()->bottom();
        }
    }

    return std::max(ret_width, m_max_line_width);
}

void litehtml::render_item_inline_context::fix_line_width( int max_width, element_float flt )
{
    int ret_width = 0;
    if(!m_line_boxes.empty())
    {
		auto el_front = m_line_boxes.back()->get_first_text_part();

        std::vector<std::shared_ptr<render_item>> els;
        bool was_cleared = false;
        if(el_front && el_front->src_el()->css().get_clear() != clear_none)
        {
            if(el_front->src_el()->css().get_clear() == clear_both)
            {
                was_cleared = true;
            } else
            {
                if(	(flt == float_left	&& el_front->src_el()->css().get_clear() == clear_left) ||
                       (flt == float_right	&& el_front->src_el()->css().get_clear() == clear_right) )
                {
                    was_cleared = true;
                }
            }
        }

        if(!was_cleared)
        {
			std::list<std::unique_ptr<line_box_item> > items = std::move(m_line_boxes.back()->items());
            m_line_boxes.pop_back();

            for(auto& item : items)
            {
                place_inline(std::move(item), max_width);
            }
        } else
        {
            int line_top = 0;
            line_top = m_line_boxes.back()->top();

            int line_left	= 0;
            int line_right	= max_width;
            get_line_left_right(line_top, max_width, line_left, line_right);

            if(m_line_boxes.size() == 1)
            {
                if (src_el()->css().get_list_style_type() != list_style_type_none && src_el()->css().get_list_style_position() == list_style_position_inside)
                {
                    int sz_font = src_el()->css().get_font_size();
                    line_left += sz_font;
                }

                if (src_el()->css().get_text_indent().val() != 0)
                {
                    line_left += src_el()->css().get_text_indent().calc_percent(max_width);
                }
            
            }

            auto items = m_line_boxes.back()->new_width(line_left, line_right);
            for(auto& item : items)
            {
                place_inline(std::move(item), max_width);
            }
        }
    }
}

std::list<std::unique_ptr<litehtml::line_box_item> > litehtml::render_item_inline_context::finish_last_box(bool end_of_render, int max_width)
{
	std::list<std::unique_ptr<line_box_item> > ret;

    if(!m_line_boxes.empty())
    {
		ret = m_line_boxes.back()->finish(end_of_render);

        if(m_line_boxes.back()->is_empty() && end_of_render)
        {
			// remove the last empty line
            m_line_boxes.pop_back();
        } else
		{
			m_max_line_width = std::max(m_max_line_width,
										m_line_boxes.back()->min_width() + (max_width - m_line_boxes.back()->line_right()));
		}
    }
    return ret;
}

int litehtml::render_item_inline_context::new_box(const std::unique_ptr<line_box_item>& el, int max_width, line_context& line_ctx)
{
	auto items = finish_last_box(false, max_width);
	int line_top = 0;
	if(!m_line_boxes.empty())
	{
		line_top = m_line_boxes.back()->bottom();
	}
    line_ctx.top = get_cleared_top(el->get_el(), line_top);

    line_ctx.left = 0;
    line_ctx.right = max_width;
    line_ctx.fix_top();
    get_line_left_right(line_ctx.top, max_width, line_ctx.left, line_ctx.right);

    if(el->get_el()->src_el()->is_inline_box() || el->get_el()->src_el()->is_floats_holder())
    {
        if (el->get_el()->width() > line_ctx.right - line_ctx.left)
        {
            line_ctx.top = find_next_line_top(line_ctx.top, el->get_el()->width(), max_width);
            line_ctx.left = 0;
            line_ctx.right = max_width;
            line_ctx.fix_top();
            get_line_left_right(line_ctx.top, max_width, line_ctx.left, line_ctx.right);
        }
    }

    int first_line_margin = 0;
    int text_indent = 0;
    if(m_line_boxes.empty())
    {
        if(src_el()->css().get_list_style_type() != list_style_type_none && src_el()->css().get_list_style_position() == list_style_position_inside)
        {
            int sz_font = src_el()->css().get_font_size();
            first_line_margin = sz_font;
        }
        if(src_el()->css().get_text_indent().val() != 0)
        {
            text_indent = src_el()->css().get_text_indent().calc_percent(max_width);
        }
    }

    m_line_boxes.emplace_back(std::unique_ptr<line_box>(new line_box(
			line_ctx.top,
			line_ctx.left + first_line_margin + text_indent, line_ctx.right,
			css().get_line_height(),
			css().get_font_metrics(),
			css().get_text_align())));

	// Add items returned by finish_last_box function into the new line
	for(auto& it : items)
	{
		m_line_boxes.back()->add_item(std::move(it));
	}

    return line_ctx.top;
}

void litehtml::render_item_inline_context::place_inline(std::unique_ptr<line_box_item> item, int max_width)
{
    if(item->get_el()->src_el()->css().get_display() == display_none) return;

    if(item->get_el()->src_el()->is_float())
    {
        int line_top = 0;
        if(!m_line_boxes.empty())
        {
            line_top = m_line_boxes.back()->top();
        }
        int ret = place_float(item->get_el(), line_top, max_width);
		if(ret > m_max_line_width)
		{
			m_max_line_width = ret;
		}
		return;
    }

    line_context line_ctx = {0};
    line_ctx.top = 0;
    if (!m_line_boxes.empty())
    {
        line_ctx.top = m_line_boxes.back().get()->top();
    }
    line_ctx.left = 0;
    line_ctx.right = max_width;
    line_ctx.fix_top();
    get_line_left_right(line_ctx.top, max_width, line_ctx.left, line_ctx.right);

	if(item->get_type() == line_box_item::type_text_part)
	{
		switch (item->get_el()->src_el()->css().get_display())
		{
			case display_inline_block:
			case display_inline_table:
				item->get_el()->render(line_ctx.left, line_ctx.top, line_ctx.right);
				break;
			case display_inline_text:
			{
				litehtml::size sz;
				item->get_el()->src_el()->get_content_size(sz, line_ctx.right);
				item->get_el()->pos() = sz;
			}
				break;
		}
	}

    bool add_box = true;
    if(!m_line_boxes.empty())
    {
        if(m_line_boxes.back()->can_hold(item, src_el()->css().get_white_space()))
        {
            add_box = false;
        }
    }
    if(add_box)
    {
        new_box(item, max_width, line_ctx);
    } else if(!m_line_boxes.empty())
    {
        line_ctx.top = m_line_boxes.back()->top();
    }

    if (line_ctx.top != line_ctx.calculatedTop)
    {
        line_ctx.left = 0;
        line_ctx.right = max_width;
        line_ctx.fix_top();
        get_line_left_right(line_ctx.top, max_width, line_ctx.left, line_ctx.right);
    }

    if(!item->get_el()->src_el()->is_inline_box())
    {
        if(m_line_boxes.size() == 1)
        {
            if(collapse_top_margin())
            {
                int shift = item->get_el()->margin_top();
                if(shift >= 0)
                {
                    line_ctx.top -= shift;
                    m_line_boxes.back()->y_shift(-shift);
                }
            }
        } else
        {
            int shift = 0;
            int prev_margin = m_line_boxes[m_line_boxes.size() - 2]->bottom_margin();

            if(prev_margin > item->get_el()->margin_top())
            {
                shift = item->get_el()->margin_top();
            } else
            {
                shift = prev_margin;
            }
            if(shift >= 0)
            {
                line_ctx.top -= shift;
                m_line_boxes.back()->y_shift(-shift);
            }
        }
    }

	m_line_boxes.back()->add_item(std::move(item));
}

void litehtml::render_item_inline_context::apply_vertical_align()
{
    if(!m_line_boxes.empty())
    {
        int add = 0;
        int content_height	= m_line_boxes.back()->bottom();

        if(m_pos.height > content_height)
        {
            switch(src_el()->css().get_vertical_align())
            {
                case va_middle:
                    add = (m_pos.height - content_height) / 2;
                    break;
                case va_bottom:
                    add = m_pos.height - content_height;
                    break;
                default:
                    add = 0;
                    break;
            }
        }

        if(add)
        {
            for(auto & box : m_line_boxes)
            {
                box->y_shift(add);
            }
        }
    }
}

int litehtml::render_item_inline_context::get_base_line()
{
    auto el_parent = parent();
    if(el_parent && src_el()->css().get_display() == display_inline_flex)
    {
        return el_parent->get_base_line();
    }
    if(src_el()->is_replaced())
    {
        return 0;
    }
    int bl = 0;
    if(!m_line_boxes.empty())
    {
        bl = m_line_boxes.back()->baseline() + content_offset_bottom();
    }
    return bl;
}
