"""Shared physical geometry for generated Rack panel tests."""


def centered_stroke_outer_radius(radius_mm, stroke_width_mm):
    return float(radius_mm) + float(stroke_width_mm) / 2.0


def visible_output_material(radius_mm, stroke_width_mm,
                            obscuring_radius_mm):
    return (
        centered_stroke_outer_radius(radius_mm, stroke_width_mm)
        - float(obscuring_radius_mm)
    )


def centered_text_clearance(offset_mm, component_outer_radius_mm,
                            font_size_px, pixels_per_mm):
    half_text_height_mm = float(font_size_px) / (2.0 * float(pixels_per_mm))
    return (
        float(offset_mm)
        - float(component_outer_radius_mm)
        - half_text_height_mm
    )
