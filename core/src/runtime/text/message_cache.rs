//! Exact live-message comparison without a hash lookup for every font field.
//! Public MessageLayer inputs stay mutable; no generation or pointer shortcut.
use crate::text::backlog::{BacklogPage, BacklogTag};
use crate::text::render::MessageLayer;
use std::collections::HashMap;

struct FontInput(Vec<(String, String)>);

impl FontInput {
    fn capture(font: &HashMap<String, String>) -> Self {
        Self(
            font.iter()
                .map(|(key, value)| (key.clone(), value.clone()))
                .collect(),
        )
    }

    fn matches(&self, font: &HashMap<String, String>) -> bool {
        // Iteration order is not a semantic assumption: reordered maps miss
        // conservatively and refresh. Equal length + every key/value equal is
        // sufficient for a hit, even after direct in-place String mutation.
        self.0.len() == font.len()
            && self
                .0
                .iter()
                .zip(font.iter())
                .all(|((a, b), (key, value))| a == key && b == value)
    }
}

enum TagInput {
    Plain(BacklogTag),
    Font(FontInput),
}

impl TagInput {
    fn capture(tag: &BacklogTag) -> Self {
        match tag {
            BacklogTag::Font(font) => Self::Font(FontInput::capture(font)),
            _ => Self::Plain(tag.clone()),
        }
    }

    fn matches(&self, tag: &BacklogTag) -> bool {
        match (self, tag) {
            (Self::Font(cached), BacklogTag::Font(font)) => cached.matches(font),
            (Self::Plain(cached), current) => cached == current,
            _ => false,
        }
    }
}

enum Input {
    Ordered {
        font: FontInput,
        tags: Vec<TagInput>,
    },
    Legacy(BacklogPage),
}

pub(super) struct MessageInput(Input);

impl MessageInput {
    pub(super) fn capture(layer: &MessageLayer, ordered: bool) -> Self {
        Self(if ordered {
            Input::Ordered {
                font: FontInput::capture(&layer.page_font),
                tags: layer.page_tags.iter().map(TagInput::capture).collect(),
            }
        } else {
            Input::Legacy(BacklogPage {
                page_font: Some(layer.page_font.clone()),
                tags: layer.page_tags.clone(),
            })
        })
    }

    pub(super) fn matches(&self, layer: &MessageLayer) -> bool {
        match &self.0 {
            Input::Ordered { font, tags } => {
                font.matches(&layer.page_font)
                    && tags.len() == layer.page_tags.len()
                    && tags
                        .iter()
                        .zip(&layer.page_tags)
                        .all(|(cached, current)| cached.matches(current))
            }
            Input::Legacy(cached) => {
                cached.page_font.as_ref() == Some(&layer.page_font)
                    && cached.tags == layer.page_tags
            }
        }
    }
}
