//! Presentation-only message roles resolved by the game's own Lua helper.
//! The native engine treats chgmsg IDs as opaque strings, not semantic names.
use crate::Interpreter;
use mlua::Value;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MessageLayerIds {
    pub name: Option<String>,
    pub dialogue: Option<String>,
    pub subtitle: Option<String>,
}

impl Interpreter {
    /// None: legacy script has no resolver. Some(empty): resolver explicitly
    /// exposes no text layers. Error: preserve the last working mapping.
    /// Called at text-layer/setup boundaries, never from the drawing loop.
    pub fn query_message_layer_ids(&self) -> mlua::Result<Option<MessageLayerIds>> {
        let value: Value = self.lua().globals().raw_get("mw_getmsgid")?;
        let Value::Function(resolve) = value else { return Ok(None) };
        let id = |role| -> mlua::Result<Option<String>> {
            match resolve.call::<Value>(role)? {
                Value::Nil | Value::Boolean(false) => Ok(None),
                Value::String(s) => {
                    let s = s.to_str()?.to_string();
                    if s.is_empty() { return Ok(None); }
                    if s.len() > 1024 || s.contains('\0') {
                        return Err(mlua::Error::RuntimeError("invalid message layer ID".into()));
                    }
                    Ok(Some(s))
                }
                _ => Err(mlua::Error::RuntimeError("message layer resolver must return a string or nil".into())),
            }
        };
        let mut ids = MessageLayerIds { name: id("name")?, dialogue: id("adv")?, subtitle: id("sub")? };
        // A shared name/body layer is ambiguous. Never choose an arbitrary
        // override or leak its classification into other UI text.
        if ids.name.is_some() && (ids.name == ids.dialogue || ids.name == ids.subtitle) {
            let conflict = ids.name.take();
            if ids.dialogue == conflict { ids.dialogue = None; }
            if ids.subtitle == conflict { ids.subtitle = None; }
        }
        Ok(Some(ids))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn role_mapping_uses_resolver_values_not_id_spelling() {
        let it=Interpreter::default();
        assert_eq!(it.query_message_layer_ids().unwrap(), None);
        it.lua().load(r#"box='ui.custom'; function mw_getmsgid(role)
          return ({name=box..'.speaker',adv=box..'.paragraph',sub=box..'.translation'})[role] end"#).exec().unwrap();
        let ids=it.query_message_layer_ids().unwrap().unwrap();
        assert_eq!(ids.name.as_deref(),Some("ui.custom.speaker"));
        assert_eq!(ids.dialogue.as_deref(),Some("ui.custom.paragraph"));
        it.lua().load("box='new-language'").exec().unwrap();
        assert_eq!(it.query_message_layer_ids().unwrap().unwrap().subtitle.as_deref(),Some("new-language.translation"));
    }
    #[test]
    fn nil_error_bad_type_and_conflict_are_not_legacy_fallbacks() {
        let it=Interpreter::default();
        it.lua().load("function mw_getmsgid(r) return nil end").exec().unwrap();
        assert_eq!(it.query_message_layer_ids().unwrap(),Some(MessageLayerIds::default()));
        for body in ["error('not ready')", "return {}", "return string.rep('x',1025)", "return string.char(0)"] {
            it.lua().load(format!("function mw_getmsgid(r) {body} end")).exec().unwrap();
            assert!(it.query_message_layer_ids().is_err());
        }
        it.lua().load("function mw_getmsgid(r) return r=='sub' and 'subtitle' or 'shared' end").exec().unwrap();
        assert_eq!(it.query_message_layer_ids().unwrap(),Some(MessageLayerIds{name:None,dialogue:None,subtitle:Some("subtitle".into())}));
    }
}
