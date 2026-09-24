
DROP TABLE IF EXISTS games;

CREATE TABLE games(
    id BIGSERIAL PRIMARY KEY,
    name TEXT,
    description TEXT
);

CREATE OR REPLACE FUNCTION notify_games_updated() RETURNS TRIGGER AS $$
DECLARE
  changed_id BIGINT;
BEGIN
  -- Get the ID of interest
  IF (TG_OP = 'DELETE') THEN
    changed_id := OLD.id;
  ELSE
    changed_id := NEW.id;
  END IF;

  -- TG_OP is uppercase and we want lowercase
  PERFORM pg_notify(
    'games_updated',
    json_build_object('type', lower(TG_OP), 'id', changed_id)::text
  );
  RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER on_updated_games
AFTER INSERT OR UPDATE OR DELETE ON games
FOR EACH ROW
EXECUTE FUNCTION notify_games_updated();

